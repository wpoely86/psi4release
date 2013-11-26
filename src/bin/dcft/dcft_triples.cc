/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#include <libtrans/integraltransform.h>
#include <libpsio/psio.hpp>
#include <libqt/qt.h>
#include <libiwl/iwl.h>
#include "dcft.h"
#include "defines.h"

namespace psi{ namespace dcft{

double
DCFTSolver::compute_three_particle_energy()
{

    fprintf(outfile, "\n\tEvaluating three-particle energy correction...\n\n");

    // Tansform OOOV and VVVO two-electron integrals and two-particle cumulants to semicanonical basis
    dcft_semicanonicalize();

    // Compute the three-particle energy contributions
    double triples_energy_aaa = compute_triples_aaa();
    double triples_energy_aab = compute_triples_aab();
    double triples_energy_abb = compute_triples_abb();
    double triples_energy_bbb = compute_triples_bbb();

    fprintf(outfile,   "\t*DCFT Three-particle Energy (AAA)                = %20.15f\n", triples_energy_aaa);
    fprintf(outfile,   "\t*DCFT Three-particle Energy (AAB)                = %20.15f\n", triples_energy_aab);
    fprintf(outfile,   "\t*DCFT Three-particle Energy (ABB)                = %20.15f\n", triples_energy_abb);
    fprintf(outfile,   "\t*DCFT Three-particle Energy (BBB)                = %20.15f\n", triples_energy_bbb);

    return triples_energy_aaa + triples_energy_aab + triples_energy_abb + triples_energy_bbb;

}

void
DCFTSolver::dcft_semicanonicalize()
{

    // Compute transformation to the semicanonical basis and dump it to disk
    dump_semicanonical();

    // Transform <OV||VV> integrals to the semicanonical basis
    semicanonicalize_gbar_ovvv();

    // Transform <OO||OV> integrals to the semicanonical basis
    semicanonicalize_gbar_ooov();

    // Transform OOVV cumulants to the semicanonical basis
    semicanonicalize_dc();

}


void
DCFTSolver::dump_semicanonical()
{
    // Copy core hamiltonian into the Fock matrix array: F = H
    Fa_->copy(so_h_);
    Fb_->copy(so_h_);
    // Build the new Fock matrix from the SO integrals: F += Gbar * Kappa
    process_so_ints();
    // Form F0 matrix
    moF0a_->copy(Fa_);
    moF0b_->copy(Fb_);
    // Transform the F0 matrix to the MO basis
    moF0a_->transform(Ca_);
    moF0b_->transform(Cb_);
    // Zero out occupied-virtual part
    for(int h = 0; h < nirrep_; ++h){
        for(int i = 0; i < naoccpi_[h]; ++i){
            for(int a = naoccpi_[h]; a < nmopi_[h]; ++a){
                moF0a_->set(h, i, a, 0.0);
                moF0a_->set(h, a, i, 0.0);
            }
        }
        for(int i = 0; i < nboccpi_[h]; ++i){
            for(int a = nboccpi_[h]; a < nmopi_[h]; ++a){
                moF0b_->set(h, i, a, 0.0);
                moF0b_->set(h, a, i, 0.0);
            }
        }
    }

    // Diagonalize F0 to get transformation matrix to semicanonical basis
    SharedMatrix a_evecs (new Matrix ("F0 Eigenvectors (Alpha)", nirrep_, nmopi_, nmopi_));
    SharedMatrix b_evecs (new Matrix ("F0 Eigenvectors (Beta)", nirrep_, nmopi_, nmopi_));
    SharedVector a_evals (new Vector ("F0 Eigenvalues (Alpha)", nirrep_, nmopi_));
    SharedVector b_evals (new Vector ("F0 Eigenvalues (Beta)", nirrep_, nmopi_));

    moF0a_->diagonalize(a_evecs, a_evals);
    moF0a_->zero();
    moF0a_->set_diagonal(a_evals);
    moF0b_->diagonalize(b_evecs, b_evals);
    moF0b_->zero();
    moF0b_->set_diagonal(b_evals);

    // Write the transformation matrix to disk
    dpdfile2 U_OO, U_VV, U_oo, U_vv;
    global_dpd_->file2_init(&U_OO, PSIF_DCFT_DPD, 0, ID('O'), ID('O'), "U <O|O>");
    global_dpd_->file2_init(&U_oo, PSIF_DCFT_DPD, 0, ID('o'), ID('o'), "U <o|o>");
    global_dpd_->file2_init(&U_VV, PSIF_DCFT_DPD, 0, ID('V'), ID('V'), "U <V|V>");
    global_dpd_->file2_init(&U_vv, PSIF_DCFT_DPD, 0, ID('v'), ID('v'), "U <v|v>");

    global_dpd_->file2_mat_init(&U_OO);
    global_dpd_->file2_mat_init(&U_oo);
    global_dpd_->file2_mat_init(&U_VV);
    global_dpd_->file2_mat_init(&U_vv);

    for(int h = 0; h < nirrep_; ++h){
        if(nsopi_[h] == 0) continue;

        // Alpha occupied
        for(int ip = 0 ; ip < naoccpi_[h]; ++ip){
            for(int kp = 0 ; kp < naoccpi_[h]; ++kp){
                U_OO.matrix[h][ip][kp] = a_evecs->get(h, ip, kp);
            }
        }

        // Beta occupied
        for(int ip = 0 ; ip < nboccpi_[h]; ++ip){
            for(int kp = 0 ; kp < nboccpi_[h]; ++kp){
                U_oo.matrix[h][ip][kp] = b_evecs->get(h, ip, kp);
            }
        }

        // Alpha virtual
        for(int ap = 0 ; ap < navirpi_[h]; ++ap){
            for(int dp = 0 ; dp < navirpi_[h]; ++dp){
                U_VV.matrix[h][ap][dp] = a_evecs->get(h, ap + naoccpi_[h], dp + naoccpi_[h]);
            }
        }

        // Beta virtual
        for(int ap = 0 ; ap < nbvirpi_[h]; ++ap){
            for(int dp = 0 ; dp < nbvirpi_[h]; ++dp){
                U_vv.matrix[h][ap][dp] = b_evecs->get(h, ap + nboccpi_[h], dp + nboccpi_[h]);
            }
        }
    }

    global_dpd_->file2_mat_wrt(&U_OO);
    global_dpd_->file2_mat_wrt(&U_oo);
    global_dpd_->file2_mat_wrt(&U_VV);
    global_dpd_->file2_mat_wrt(&U_vv);

    global_dpd_->file2_close(&U_OO);
    global_dpd_->file2_close(&U_oo);
    global_dpd_->file2_close(&U_VV);
    global_dpd_->file2_close(&U_vv);
}

void
DCFTSolver::semicanonicalize_gbar_ovvv(){

    psio_->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);

    dpdbuf4 I, It;
    dpdfile2 U_VV, U_OO, U_vv, U_oo;

    // TODO: The transformed tensors should be packed on disk wherever possible

    global_dpd_->file2_init(&U_VV, PSIF_DCFT_DPD, 0, ID('V'), ID('V'), "U <V|V>");
    global_dpd_->file2_init(&U_OO, PSIF_DCFT_DPD, 0, ID('O'), ID('O'), "U <O|O>");
    global_dpd_->file2_init(&U_vv, PSIF_DCFT_DPD, 0, ID('v'), ID('v'), "U <v|v>");
    global_dpd_->file2_init(&U_oo, PSIF_DCFT_DPD, 0, ID('o'), ID('o'), "U <o|o>");

    //
    // Alpha OVVV
    //

    // <IA||BC> * U_CC' -> <IA||BC'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 1, "MO Ints <OV|VV>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV||VV'>");
    global_dpd_->contract424(&I, &U_VV, &It, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&I, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_BB' * <IA||BC'> -> <IA||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV||VV'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV||V'V'>");
    global_dpd_->contract244(&U_VV, &I, &It, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <IA||B'C'> * U_AA' -> <IA'||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV||V'V'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV'||V'V'>");
    global_dpd_->contract424(&I, &U_VV, &It, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_II' * <IA'||B'C'> -> <I'A'||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <OV'||V'V'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                  ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <O'V'||V'V'>");
    global_dpd_->contract244(&U_OO, &I, &It, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&It, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);


    //
    // Beta OVVV
    //

    global_dpd_->file2_print(&U_vv, outfile);
    global_dpd_->file2_print(&U_oo, outfile);

    // <ia||bc> * U_cc' -> <ia||bc'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 1, "MO Ints <ov|vv>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov||vv'>");
    global_dpd_->contract424(&I, &U_vv, &It, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&I, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_bb' * <ia||bc'> -> <ia||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov||vv'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov||v'v'>");
    global_dpd_->contract244(&U_vv, &I, &It, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <ia||b'c'> * U_aa' -> <ia'||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov||v'v'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov'||v'v'>");
    global_dpd_->contract424(&I, &U_vv, &It, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_ii' * <ia'||b'c'> -> <i'a'||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <ov'||v'v'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                  ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <o'v'||v'v'>");
    global_dpd_->contract244(&U_oo, &I, &It, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&It, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    ////////////////////////////////

    global_dpd_->file2_close(&U_VV);
    global_dpd_->file2_close(&U_OO);
    global_dpd_->file2_close(&U_vv);
    global_dpd_->file2_close(&U_oo);

    psio_->close(PSIF_LIBTRANS_DPD, 1);

}

void
DCFTSolver::semicanonicalize_gbar_ooov(){

    psio_->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);

    dpdbuf4 I, It;
    dpdfile2 U_VV, U_OO, U_vv, U_oo;

    // TODO: The transformed tensors should be packed on disk wherever possible

    global_dpd_->file2_init(&U_VV, PSIF_DCFT_DPD, 0, ID('V'), ID('V'), "U <V|V>");
    global_dpd_->file2_init(&U_OO, PSIF_DCFT_DPD, 0, ID('O'), ID('O'), "U <O|O>");
    global_dpd_->file2_init(&U_vv, PSIF_DCFT_DPD, 0, ID('v'), ID('v'), "U <v|v>");
    global_dpd_->file2_init(&U_oo, PSIF_DCFT_DPD, 0, ID('o'), ID('o'), "U <o|o>");

    //
    // Alpha VOOO
    //

    // <IA||BC> * U_CC' -> <AA||BC'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 1, "MO Ints <VO|OO>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO||OO'>");
    global_dpd_->contract424(&I, &U_OO, &It, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&I, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_BB' * <IA||BC'> -> <IA||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO||OO'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO||O'O'>");
    global_dpd_->contract244(&U_OO, &I, &It, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <IA||B'C'> * U_AA' -> <IA'||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO||O'O'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO'||O'O'>");
    global_dpd_->contract424(&I, &U_OO, &It, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_II' * <IA'||B'C'> -> <I'A'||B'C'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <VO'||O'O'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <V'O'||O'O'>");
    global_dpd_->contract244(&U_VV, &I, &It, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&It, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <I'A'||B'C'> -> <C'B'||A'I'>
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[V,O]"), ID("[O,O]"),
                  ID("[V,O]"), ID("[O,O]"), 0, "MO Ints <V'O'||O'O'>");
    global_dpd_->buf4_sort(&It, PSIF_LIBTRANS_DPD, srqp, ID("[O,O]"), ID("[O,V]"), "MO Ints <O'O'||O'V'>");
    global_dpd_->buf4_close(&It);

    //
    // Beta VOOO
    //

    // <ia||bc> * U_cc' -> <ia||bc'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 1, "MO Ints <vo|oo>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo||oo'>");
    global_dpd_->contract424(&I, &U_oo, &It, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&I, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_bb' * <ia||bc'> -> <IA||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo||oo'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo||o'o'>");
    global_dpd_->contract244(&U_oo, &I, &It, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <ia||b'c'> * U_aa' -> <ia'||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo||o'o'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo'||o'o'>");
    global_dpd_->contract424(&I, &U_oo, &It, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // U_ii' * <ia'||b'c'> -> <i'a'||b'c'>
    global_dpd_->buf4_init(&I, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <vo'||o'o'>");
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <v'o'||o'o'>");
    global_dpd_->contract244(&U_vv, &I, &It, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&It, outfile, 1);
    global_dpd_->buf4_close(&It);
    global_dpd_->buf4_close(&I);

    // <i'a'||b'c'> -> <c'b'||a'i'>
    global_dpd_->buf4_init(&It, PSIF_LIBTRANS_DPD, 0, ID("[v,o]"), ID("[o,o]"),
                  ID("[v,o]"), ID("[o,o]"), 0, "MO Ints <v'o'||o'o'>");
    global_dpd_->buf4_sort(&It, PSIF_LIBTRANS_DPD, srqp, ID("[o,o]"), ID("[o,v]"), "MO Ints <o'o'||o'v'>");
    global_dpd_->buf4_close(&It);

    ////////////////////////////////

    global_dpd_->file2_close(&U_VV);
    global_dpd_->file2_close(&U_OO);
    global_dpd_->file2_close(&U_vv);
    global_dpd_->file2_close(&U_oo);

    psio_->close(PSIF_LIBTRANS_DPD, 1);

}

void
DCFTSolver::semicanonicalize_dc(){

    dpdbuf4 L, Lt;
    dpdfile2 U_VV, U_OO, U_vv, U_oo;

    // TODO: The transformed tensors should be packed on disk wherever possible

    global_dpd_->file2_init(&U_VV, PSIF_DCFT_DPD, 0, ID('V'), ID('V'), "U <V|V>");
    global_dpd_->file2_init(&U_OO, PSIF_DCFT_DPD, 0, ID('O'), ID('O'), "U <O|O>");
    global_dpd_->file2_init(&U_vv, PSIF_DCFT_DPD, 0, ID('v'), ID('v'), "U <v|v>");
    global_dpd_->file2_init(&U_oo, PSIF_DCFT_DPD, 0, ID('o'), ID('o'), "U <o|o>");

    //
    // Alpha Lambda_OOVV
    //

    // Lambda <IJ|AB> * U_BB' -> Lambda <IJ|AB'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O>O]-"), ID("[V>V]-"), 0, "Lambda <OO|VV>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV'>");
    global_dpd_->contract424(&L, &U_VV, &Lt, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <IJ|AB'> * U_AA' -> Lambda <IJ|A'B'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|VV'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|V'V'>");
    global_dpd_->contract244(&U_VV, &L, &Lt, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <IJ|A'B'> * U_JJ' -> Lambda <IJ'|A'B'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO|V'V'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO'|V'V'>");
    global_dpd_->contract424(&L, &U_OO, &Lt, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <IJ'|A'B'> * U_II' -> Lambda <I'J'|A'B'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <OO'|V'V'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                  ID("[O,O]"), ID("[V,V]"), 0, "Lambda <O'O'|V'V'>");
    global_dpd_->contract244(&U_OO, &L, &Lt, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&Lt, outfile, 1);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    //
    // Beta Lambda_OOVV
    //

    // Lambda <ij|ab> * U_bb' -> Lambda <ij|ab'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o>o]-"), ID("[v>v]-"), 0, "Lambda <oo|vv>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv'>");
    global_dpd_->contract424(&L, &U_vv, &Lt, 3, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <ij|ab'> * U_aa' -> Lambda <ij|a'b'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|vv'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|v'v'>");
    global_dpd_->contract244(&U_vv, &L, &Lt, 0, 2, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <ij|a'b'> * U_jj' -> Lambda <ij'|a'b'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo|v'v'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo'|v'v'>");
    global_dpd_->contract424(&L, &U_oo, &Lt, 1, 0, 1, 1.0, 0.0);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    // Lambda <ij'|a'b'> * U_ii' -> Lambda <i'j'|a'b'>
    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <oo'|v'v'>");
    global_dpd_->buf4_init(&Lt, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                  ID("[o,o]"), ID("[v,v]"), 0, "Lambda <o'o'|v'v'>");
    global_dpd_->contract244(&U_oo, &L, &Lt, 0, 0, 0, 1.0, 0.0);
    global_dpd_->buf4_print(&Lt, outfile, 1);
    global_dpd_->buf4_close(&Lt);
    global_dpd_->buf4_close(&L);

    ////////////////////////////////

    global_dpd_->file2_close(&U_VV);
    global_dpd_->file2_close(&U_OO);
    global_dpd_->file2_close(&U_vv);
    global_dpd_->file2_close(&U_oo);

}

double
DCFTSolver::compute_triples_aaa()
{

    int h;
    int Gi, Gj, Gk, Ga, Gb, Gc, Gd, Gl;
    int Gji, Gij, Gjk, Gkj, Gik, Gki, Gijk;
    int Gab, Gbc, Gac;
    int Gid, Gjd, Gkd;
    int Gil, Gjl, Gkl;
    int I, J, K, A, B, C;
    int i, j, k, a, b, c;
    int ij, ji, ik, ki, jk, kj;
    int ab;
    int cd, ad, bd;
    int id, jd, kd;
    int il, jl, kl;
    int lc, la, lb;
    double dijk, denom, ET_aaa;
    int nrows, ncols, nlinks;
    dpdbuf4 L, I_OVVV, I_OOOV;
    double ***WABC, ***WBCA, ***WACB, ***LABC;

    psio_->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);

    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[O,O]"), ID("[V,V]"),
                           ID("[O,O]"), ID("[V,V]"), 0, "Lambda <O'O'|V'V'>");
    global_dpd_->buf4_init(&I_OVVV, PSIF_LIBTRANS_DPD, 0, ID("[O,V]"), ID("[V,V]"),
                           ID("[O,V]"), ID("[V,V]"), 0, "MO Ints <O'V'||V'V'>");
    global_dpd_->buf4_init(&I_OOOV, PSIF_LIBTRANS_DPD, 0, ID("[O,O]"), ID("[O,V]"),
                           ID("[O,O]"), ID("[O,V]"), 0, "MO Ints <O'O'||O'V'>");

    for(h=0; h < nirrep_; h++) {
      global_dpd_->buf4_mat_irrep_init(&L, h);
      global_dpd_->buf4_mat_irrep_rd(&L, h);

      global_dpd_->buf4_mat_irrep_init(&I_OOOV, h);
      global_dpd_->buf4_mat_irrep_rd(&I_OOOV, h);
    }

    WABC = (double ***) malloc(nirrep_ * sizeof(double **));
    LABC = (double ***) malloc(nirrep_ * sizeof(double **));
    WBCA = (double ***) malloc(nirrep_ * sizeof(double **));
    WACB = (double ***) malloc(nirrep_ * sizeof(double **));

    ET_aaa = 0.0;

    for(Gi=0; Gi < nirrep_; Gi++) {
      for(Gj=0; Gj < nirrep_; Gj++) {
        for(Gk=0; Gk < nirrep_; Gk++) {

      Gij = Gji = Gi ^ Gj;
      Gjk = Gkj = Gj ^ Gk;
      Gik = Gki = Gi ^ Gk;

      Gijk = Gi ^ Gj ^ Gk;

      for(i=0; i < naoccpi_[Gi]; i++) {
        I = aocc_off_[Gi] + i;
        for(j=0; j < naoccpi_[Gj]; j++) {
          J = aocc_off_[Gj] + j;
          for(k=0; k < naoccpi_[Gk]; k++) {
            K = aocc_off_[Gk] + k;

            if(I > J && J > K) {

          ij = I_OOOV.params->rowidx[I][J];
          ji = I_OOOV.params->rowidx[J][I];
          jk = I_OOOV.params->rowidx[J][K];
          kj = I_OOOV.params->rowidx[K][J];
          ik = I_OOOV.params->rowidx[I][K];
          ki = I_OOOV.params->rowidx[K][I];

          dijk = 0.0;
          if(moF0a_->rowspi()[Gi])
            dijk += moF0a_->get(Gi, i, i);
          if(moF0a_->rowspi()[Gj])
            dijk += moF0a_->get(Gj, j, j);
          if(moF0a_->rowspi()[Gk])
            dijk += moF0a_->get(Gk, k, k);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            WABC[Gab] = global_dpd_->dpd_block_matrix(I_OVVV.params->coltot[Gab], navirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* -lambda_jkcd * <id||ab> */
            Gab = Gid = Gi ^ Gd;
            Gc = Gjk ^ Gd;

            cd = L.col_offset[Gjk][Gc];
            id = I_OVVV.row_offset[Gid][I];

            I_OVVV.matrix[Gid] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gid, id, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gid];
            ncols = navirpi_[Gc];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_OVVV.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gid], navirpi_[Gd], I_OVVV.params->coltot[Gid]);

            /* +lambda_ikcd * <jd||ab> */
            Gab = Gjd = Gj ^ Gd;
            Gc = Gik ^ Gd;

            cd = L.col_offset[Gik][Gc];
            jd = I_OVVV.row_offset[Gjd][J];

            I_OVVV.matrix[Gjd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gjd, jd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gjd];
            ncols = navirpi_[Gc];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_OVVV.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gjd], navirpi_[Gd], I_OVVV.params->coltot[Gjd]);

            /* +lambda_jicd * <kd||ab> */
            Gab = Gkd = Gk ^ Gd;
            Gc = Gji ^ Gd;

            cd = L.col_offset[Gji][Gc];
            kd = I_OVVV.row_offset[Gkd][K];

            I_OVVV.matrix[Gkd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gkd, kd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gkd];
            ncols = navirpi_[Gc];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_OVVV.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gkd], navirpi_[Gd], I_OVVV.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* -lambda_ilab <jk||lc> */
            Gab = Gil = Gi ^ Gl;
            Gc = Gjk ^ Gl;

            lc = I_OOOV.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = navirpi_[Gc];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_OOOV.matrix[Gjk][jk][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            /* +lambda_jlab <ik||lc> */
            Gab = Gjl = Gj ^ Gl;
            Gc = Gik ^ Gl;

            lc = I_OOOV.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = navirpi_[Gc];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_OOOV.matrix[Gik][ik][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            /* +lambda_klab <ji||lc> */
            Gab = Gkl = Gk ^ Gl;
            Gc = Gji ^ Gl;

            lc = I_OOOV.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = navirpi_[Gc];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_OOOV.matrix[Gji][ji][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);
          }

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            WBCA[Gab] = global_dpd_->dpd_block_matrix(I_OVVV.params->coltot[Gab], navirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* -lambda_jkad * <id||bc> */
            Gbc = Gid = Gi ^ Gd;
            Ga = Gjk ^ Gd;

            ad = L.col_offset[Gjk][Ga];
            id = I_OVVV.row_offset[Gid][I];

            I_OVVV.matrix[Gid] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gid, id, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gid];
            ncols = navirpi_[Ga];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_OVVV.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gid], navirpi_[Gd], I_OVVV.params->coltot[Gid]);

            /* +lambda_ikad * <jd||bc> */
            Gbc = Gjd = Gj ^ Gd;
            Ga = Gik ^ Gd;

            ad = L.col_offset[Gik][Ga];
            jd = I_OVVV.row_offset[Gjd][J];

            I_OVVV.matrix[Gjd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gjd, jd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gjd];
            ncols = navirpi_[Ga];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_OVVV.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gjd], navirpi_[Gd], I_OVVV.params->coltot[Gjd]);

            /* +lambda_jiad * <kd||bc> */
            Gbc = Gkd = Gk ^ Gd;
            Ga = Gji ^ Gd;

            ad = L.col_offset[Gji][Ga];
            kd = I_OVVV.row_offset[Gkd][K];

            I_OVVV.matrix[Gkd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gkd, kd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gkd];
            ncols = navirpi_[Ga];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_OVVV.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gkd], navirpi_[Gd], I_OVVV.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* -lambda_ilbc * <jk||la> */
            Gbc = Gil = Gi ^ Gl;
            Ga = Gjk ^ Gl;

            la = I_OOOV.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = navirpi_[Ga];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_OOOV.matrix[Gjk][jk][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            /* +lambda_jlbc <ik||la> */
            Gbc = Gjl = Gj ^ Gl;
            Ga = Gik ^ Gl;

            la = I_OOOV.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = navirpi_[Ga];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_OOOV.matrix[Gik][ik][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            /* +lambda_klbc <ji||la> */
            Gbc = Gkl = Gk ^ Gl;
            Ga = Gji ^ Gl;

            la = I_OOOV.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = navirpi_[Ga];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_OOOV.matrix[Gji][ji][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);
          }

          global_dpd_->sort_3d(WBCA, WABC, nirrep_, Gijk, I_OVVV.params->coltot, I_OVVV.params->colidx,
                 I_OVVV.params->colorb, I_OVVV.params->rsym, I_OVVV.params->ssym,
                 avir_off_, avir_off_, navirpi_, avir_off_, I_OVVV.params->colidx, cab, 1);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            global_dpd_->free_dpd_block(WBCA[Gab], I_OVVV.params->coltot[Gab], navirpi_[Gc]);

            WACB[Gab] = global_dpd_->dpd_block_matrix(I_OVVV.params->coltot[Gab], navirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* +lambda_jkbd * <id||ac> */
            Gac = Gid = Gi ^ Gd;
            Gb = Gjk ^ Gd;

            bd = L.col_offset[Gjk][Gb];
            id = I_OVVV.row_offset[Gid][I];

            I_OVVV.matrix[Gid] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gid, id, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gid];
            ncols = navirpi_[Gb];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_OVVV.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gid], navirpi_[Gd], I_OVVV.params->coltot[Gid]);

            /* -lambda_ikbd * <jd||ac> */
            Gac = Gjd = Gj ^ Gd;
            Gb = Gik ^ Gd;

            bd = L.col_offset[Gik][Gb];
            jd = I_OVVV.row_offset[Gjd][J];

            I_OVVV.matrix[Gjd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gjd, jd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gjd];
            ncols = navirpi_[Gb];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_OVVV.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gjd], navirpi_[Gd], I_OVVV.params->coltot[Gjd]);

            /* -lambda_jibd * <kd||ac> */
            Gac = Gkd = Gk ^ Gd;
            Gb = Gji ^ Gd;

            bd = L.col_offset[Gji][Gb];
            kd = I_OVVV.row_offset[Gkd][K];

            I_OVVV.matrix[Gkd] = global_dpd_->dpd_block_matrix(navirpi_[Gd], I_OVVV.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_OVVV, Gkd, kd, navirpi_[Gd]);

            nrows = I_OVVV.params->coltot[Gkd];
            ncols = navirpi_[Gb];
            nlinks = navirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_OVVV.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_OVVV.matrix[Gkd], navirpi_[Gd], I_OVVV.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* +lambda_ilac * <jk||lb> */
            Gac = Gil = Gi ^ Gl;
            Gb = Gjk ^ Gl;

            lb = I_OOOV.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = navirpi_[Gb];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_OOOV.matrix[Gjk][jk][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            /* -lambda_jlac * <ik||lb> */
            Gac = Gjl = Gj ^ Gl;
            Gb = Gik ^ Gl;

            lb = I_OOOV.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = navirpi_[Gb];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_OOOV.matrix[Gik][ik][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            /* -lambda_klac * <ji||lb> */
            Gac = Gkl = Gk ^ Gl;
            Gb = Gji ^ Gl;

            lb = I_OOOV.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = navirpi_[Gb];
            nlinks = naoccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_OOOV.matrix[Gji][ji][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);
          }

          global_dpd_->sort_3d(WACB, WABC, nirrep_, Gijk, I_OVVV.params->coltot, I_OVVV.params->colidx,
                 I_OVVV.params->colorb, I_OVVV.params->rsym, I_OVVV.params->ssym,
                 avir_off_, avir_off_, navirpi_, avir_off_, I_OVVV.params->colidx, acb, 1);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            global_dpd_->free_dpd_block(WACB[Gab], I_OVVV.params->coltot[Gab], navirpi_[Gc]);
          }

          /* Compute denominator and evaluate labda_ijkabc */
          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            LABC[Gab] = global_dpd_->dpd_block_matrix(I_OVVV.params->coltot[Gab], navirpi_[Gc]);

            for(ab=0; ab < I_OVVV.params->coltot[Gab]; ab++) {
              A = I_OVVV.params->colorb[Gab][ab][0];
              Ga = I_OVVV.params->rsym[A];
              a = A - avir_off_[Ga];
              B = I_OVVV.params->colorb[Gab][ab][1];
              Gb = I_OVVV.params->ssym[B];
              b = B - avir_off_[Gb];

              Gbc = Gb ^ Gc;
              Gac = Ga ^ Gc;

              for(c=0; c < navirpi_[Gc]; c++) {
                C = avir_off_[Gc] + c;

                /* Copy W into labda_ijkabc */
                LABC[Gab][ab][c] = WABC[Gab][ab][c];

                /* Build the rest of the denominator and compute labda_ijkabc */
                denom = dijk;

                if(moF0a_->rowspi()[Ga])
                    denom -= moF0a_->get(Ga, a + naoccpi_[Ga], a + naoccpi_[Ga]);
                if(moF0a_->rowspi()[Gb])
                    denom -= moF0a_->get(Gb, b + naoccpi_[Gb], b + naoccpi_[Gb]);
                if(moF0a_->rowspi()[Gc])
                    denom -= moF0a_->get(Gc, c + naoccpi_[Gc], c + naoccpi_[Gc]);

//                fprintf(outfile, "\td[%d][%d][%d][%d][%d][%d] = %20.10f\n", i, j, k, a + naoccpi_[Ga], b + naoccpi_[Gb], c + naoccpi_[Gc], denom);

                LABC[Gab][ab][c] /= denom;

              } /* c */
            } /* ab */
          } /* Gab */

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;
            ET_aaa += dot_block(LABC[Gab], WABC[Gab], I_OVVV.params->coltot[Gab], navirpi_[Gc], 1.0/6.0);
            global_dpd_->free_dpd_block(WABC[Gab], I_OVVV.params->coltot[Gab], navirpi_[Gc]);
            global_dpd_->free_dpd_block(LABC[Gab], I_OVVV.params->coltot[Gab], navirpi_[Gc]);
          }

            } /* I >= J >= K */

          } /* k */
        } /* j */
      } /* i */

        } /* Gk */
      } /* Gj */
    } /* Gi */

    free(WABC);
    free(LABC);
    free(WBCA);
    free(WACB);

    for(h=0; h < nirrep_; h++) {
      global_dpd_->buf4_mat_irrep_close(&L, h);
      global_dpd_->buf4_mat_irrep_close(&I_OOOV, h);
    }

    global_dpd_->buf4_close(&L);
    global_dpd_->buf4_close(&I_OVVV);
    global_dpd_->buf4_close(&I_OOOV);

    psio_->close(PSIF_LIBTRANS_DPD, 1);

    return ET_aaa;
}

double
DCFTSolver::compute_triples_aab()
{
    return 0.0;
}

double
DCFTSolver::compute_triples_abb()
{
    return 0.0;
}

double
DCFTSolver::compute_triples_bbb()
{

    int h;
    int Gi, Gj, Gk, Ga, Gb, Gc, Gd, Gl;
    int Gji, Gij, Gjk, Gkj, Gik, Gki, Gijk;
    int Gab, Gbc, Gac;
    int Gid, Gjd, Gkd;
    int Gil, Gjl, Gkl;
    int I, J, K, A, B, C;
    int i, j, k, a, b, c;
    int ij, ji, ik, ki, jk, kj;
    int ab;
    int cd, ad, bd;
    int id, jd, kd;
    int il, jl, kl;
    int lc, la, lb;
    double dijk, denom, ET_bbb;
    int nrows, ncols, nlinks;
    dpdbuf4 L, I_ovvv, I_ooov;
    double ***WABC, ***WBCA, ***WACB, ***LABC;

    psio_->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);

    global_dpd_->buf4_init(&L, PSIF_DCFT_DPD, 0, ID("[o,o]"), ID("[v,v]"),
                           ID("[o,o]"), ID("[v,v]"), 0, "Lambda <o'o'|v'v'>");
    global_dpd_->buf4_init(&I_ovvv, PSIF_LIBTRANS_DPD, 0, ID("[o,v]"), ID("[v,v]"),
                           ID("[o,v]"), ID("[v,v]"), 0, "MO Ints <o'v'||v'v'>");
    global_dpd_->buf4_init(&I_ooov, PSIF_LIBTRANS_DPD, 0, ID("[o,o]"), ID("[o,v]"),
                           ID("[o,o]"), ID("[o,v]"), 0, "MO Ints <o'o'||o'v'>");

    for(h=0; h < nirrep_; h++) {
      global_dpd_->buf4_mat_irrep_init(&L, h);
      global_dpd_->buf4_mat_irrep_rd(&L, h);

      global_dpd_->buf4_mat_irrep_init(&I_ooov, h);
      global_dpd_->buf4_mat_irrep_rd(&I_ooov, h);
    }

    WABC = (double ***) malloc(nirrep_ * sizeof(double **));
    LABC = (double ***) malloc(nirrep_ * sizeof(double **));
    WBCA = (double ***) malloc(nirrep_ * sizeof(double **));
    WACB = (double ***) malloc(nirrep_ * sizeof(double **));

    ET_bbb = 0.0;

    for(Gi=0; Gi < nirrep_; Gi++) {
      for(Gj=0; Gj < nirrep_; Gj++) {
        for(Gk=0; Gk < nirrep_; Gk++) {

      Gij = Gji = Gi ^ Gj;
      Gjk = Gkj = Gj ^ Gk;
      Gik = Gki = Gi ^ Gk;

      Gijk = Gi ^ Gj ^ Gk;

      for(i=0; i < nboccpi_[Gi]; i++) {
        I = bocc_off_[Gi] + i;
        for(j=0; j < nboccpi_[Gj]; j++) {
          J = bocc_off_[Gj] + j;
          for(k=0; k < nboccpi_[Gk]; k++) {
            K = bocc_off_[Gk] + k;

            if(I > J && J > K) {

          ij = I_ooov.params->rowidx[I][J];
          ji = I_ooov.params->rowidx[J][I];
          jk = I_ooov.params->rowidx[J][K];
          kj = I_ooov.params->rowidx[K][J];
          ik = I_ooov.params->rowidx[I][K];
          ki = I_ooov.params->rowidx[K][I];

          dijk = 0.0;
          if(moF0b_->rowspi()[Gi])
            dijk += moF0b_->get(Gi, i, i);
          if(moF0b_->rowspi()[Gj])
            dijk += moF0b_->get(Gj, j, j);
          if(moF0b_->rowspi()[Gk])
            dijk += moF0b_->get(Gk, k, k);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            WABC[Gab] = global_dpd_->dpd_block_matrix(I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* -lambda_jkcd * <id||ab> */
            Gab = Gid = Gi ^ Gd;
            Gc = Gjk ^ Gd;

            cd = L.col_offset[Gjk][Gc];
            id = I_ovvv.row_offset[Gid][I];

            I_ovvv.matrix[Gid] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gid, id, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gid];
            ncols = nbvirpi_[Gc];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_ovvv.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gid], nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);

            /* +lambda_ikcd * <jd||ab> */
            Gab = Gjd = Gj ^ Gd;
            Gc = Gik ^ Gd;

            cd = L.col_offset[Gik][Gc];
            jd = I_ovvv.row_offset[Gjd][J];

            I_ovvv.matrix[Gjd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gjd, jd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gjd];
            ncols = nbvirpi_[Gc];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_ovvv.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gjd], nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);

            /* +lambda_jicd * <kd||ab> */
            Gab = Gkd = Gk ^ Gd;
            Gc = Gji ^ Gd;

            cd = L.col_offset[Gji][Gc];
            kd = I_ovvv.row_offset[Gkd][K];

            I_ovvv.matrix[Gkd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gkd, kd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gkd];
            ncols = nbvirpi_[Gc];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_ovvv.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][cd]), nlinks, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gkd], nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* -lambda_ilab <jk||lc> */
            Gab = Gil = Gi ^ Gl;
            Gc = Gjk ^ Gl;

            lc = I_ooov.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = nbvirpi_[Gc];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_ooov.matrix[Gjk][jk][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            /* +lambda_jlab <ik||lc> */
            Gab = Gjl = Gj ^ Gl;
            Gc = Gik ^ Gl;

            lc = I_ooov.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = nbvirpi_[Gc];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_ooov.matrix[Gik][ik][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);

            /* +lambda_klab <ji||lc> */
            Gab = Gkl = Gk ^ Gl;
            Gc = Gji ^ Gl;

            lc = I_ooov.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = nbvirpi_[Gc];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_ooov.matrix[Gji][ji][lc]), ncols, 1.0,
                  &(WABC[Gab][0][0]), ncols);
          }

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            WBCA[Gab] = global_dpd_->dpd_block_matrix(I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* -lambda_jkad * <id||bc> */
            Gbc = Gid = Gi ^ Gd;
            Ga = Gjk ^ Gd;

            ad = L.col_offset[Gjk][Ga];
            id = I_ovvv.row_offset[Gid][I];

            I_ovvv.matrix[Gid] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gid, id, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gid];
            ncols = nbvirpi_[Ga];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_ovvv.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gid], nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);

            /* +lambda_ikad * <jd||bc> */
            Gbc = Gjd = Gj ^ Gd;
            Ga = Gik ^ Gd;

            ad = L.col_offset[Gik][Ga];
            jd = I_ovvv.row_offset[Gjd][J];

            I_ovvv.matrix[Gjd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gjd, jd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gjd];
            ncols = nbvirpi_[Ga];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_ovvv.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gjd], nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);

            /* +lambda_jiad * <kd||bc> */
            Gbc = Gkd = Gk ^ Gd;
            Ga = Gji ^ Gd;

            ad = L.col_offset[Gji][Ga];
            kd = I_ovvv.row_offset[Gkd][K];

            I_ovvv.matrix[Gkd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gkd, kd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gkd];
            ncols = nbvirpi_[Ga];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_ovvv.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][ad]), nlinks, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gkd], nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* -lambda_ilbc * <jk||la> */
            Gbc = Gil = Gi ^ Gl;
            Ga = Gjk ^ Gl;

            la = I_ooov.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = nbvirpi_[Ga];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_ooov.matrix[Gjk][jk][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            /* +lambda_jlbc <ik||la> */
            Gbc = Gjl = Gj ^ Gl;
            Ga = Gik ^ Gl;

            la = I_ooov.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = nbvirpi_[Ga];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_ooov.matrix[Gik][ik][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);

            /* +lambda_klbc <ji||la> */
            Gbc = Gkl = Gk ^ Gl;
            Ga = Gji ^ Gl;

            la = I_ooov.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = nbvirpi_[Ga];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_ooov.matrix[Gji][ji][la]), ncols, 1.0,
                  &(WBCA[Gbc][0][0]), ncols);
          }

          global_dpd_->sort_3d(WBCA, WABC, nirrep_, Gijk, I_ovvv.params->coltot, I_ovvv.params->colidx,
                 I_ovvv.params->colorb, I_ovvv.params->rsym, I_ovvv.params->ssym,
                 bvir_off_, bvir_off_, nbvirpi_, bvir_off_, I_ovvv.params->colidx, cab, 1);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            global_dpd_->free_dpd_block(WBCA[Gab], I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);

            WACB[Gab] = global_dpd_->dpd_block_matrix(I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
          }

          for(Gd=0; Gd < nirrep_; Gd++) {
            /* +lambda_jkbd * <id||ac> */
            Gac = Gid = Gi ^ Gd;
            Gb = Gjk ^ Gd;

            bd = L.col_offset[Gjk][Gb];
            id = I_ovvv.row_offset[Gid][I];

            I_ovvv.matrix[Gid] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gid, id, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gid];
            ncols = nbvirpi_[Gb];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, 1.0,
                  &(I_ovvv.matrix[Gid][0][0]), nrows,
                  &(L.matrix[Gjk][jk][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gid], nbvirpi_[Gd], I_ovvv.params->coltot[Gid]);

            /* -lambda_ikbd * <jd||ac> */
            Gac = Gjd = Gj ^ Gd;
            Gb = Gik ^ Gd;

            bd = L.col_offset[Gik][Gb];
            jd = I_ovvv.row_offset[Gjd][J];

            I_ovvv.matrix[Gjd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gjd, jd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gjd];
            ncols = nbvirpi_[Gb];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_ovvv.matrix[Gjd][0][0]), nrows,
                  &(L.matrix[Gik][ik][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gjd], nbvirpi_[Gd], I_ovvv.params->coltot[Gjd]);

            /* -lambda_jibd * <kd||ac> */
            Gac = Gkd = Gk ^ Gd;
            Gb = Gji ^ Gd;

            bd = L.col_offset[Gji][Gb];
            kd = I_ovvv.row_offset[Gkd][K];

            I_ovvv.matrix[Gkd] = global_dpd_->dpd_block_matrix(nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);
            global_dpd_->buf4_mat_irrep_rd_block(&I_ovvv, Gkd, kd, nbvirpi_[Gd]);

            nrows = I_ovvv.params->coltot[Gkd];
            ncols = nbvirpi_[Gb];
            nlinks = nbvirpi_[Gd];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 't', nrows, ncols, nlinks, -1.0,
                  &(I_ovvv.matrix[Gkd][0][0]), nrows,
                  &(L.matrix[Gji][ji][bd]), nlinks, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            global_dpd_->free_dpd_block(I_ovvv.matrix[Gkd], nbvirpi_[Gd], I_ovvv.params->coltot[Gkd]);

          }

          for(Gl=0; Gl < nirrep_; Gl++) {
            /* +lambda_ilac * <jk||lb> */
            Gac = Gil = Gi ^ Gl;
            Gb = Gjk ^ Gl;

            lb = I_ooov.col_offset[Gjk][Gl];
            il = L.row_offset[Gil][I];

            nrows = L.params->coltot[Gil];
            ncols = nbvirpi_[Gb];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, 1.0,
                  &(L.matrix[Gil][il][0]), nrows,
                  &(I_ooov.matrix[Gjk][jk][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            /* -lambda_jlac * <ik||lb> */
            Gac = Gjl = Gj ^ Gl;
            Gb = Gik ^ Gl;

            lb = I_ooov.col_offset[Gik][Gl];
            jl = L.row_offset[Gjl][J];

            nrows = L.params->coltot[Gjl];
            ncols = nbvirpi_[Gb];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gjl][jl][0]), nrows,
                  &(I_ooov.matrix[Gik][ik][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);

            /* -lambda_klac * <ji||lb> */
            Gac = Gkl = Gk ^ Gl;
            Gb = Gji ^ Gl;

            lb = I_ooov.col_offset[Gji][Gl];
            kl = L.row_offset[Gkl][K];

            nrows = L.params->coltot[Gkl];
            ncols = nbvirpi_[Gb];
            nlinks = nboccpi_[Gl];

            if(nrows && ncols && nlinks)
              C_DGEMM('t', 'n', nrows, ncols, nlinks, -1.0,
                  &(L.matrix[Gkl][kl][0]), nrows,
                  &(I_ooov.matrix[Gji][ji][lb]), ncols, 1.0,
                  &(WACB[Gac][0][0]), ncols);
          }

          global_dpd_->sort_3d(WACB, WABC, nirrep_, Gijk, I_ovvv.params->coltot, I_ovvv.params->colidx,
                 I_ovvv.params->colorb, I_ovvv.params->rsym, I_ovvv.params->ssym,
                 bvir_off_, bvir_off_, nbvirpi_, bvir_off_, I_ovvv.params->colidx, acb, 1);

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            global_dpd_->free_dpd_block(WACB[Gab], I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
          }

          /* Compute denominator and evaluate labda_ijkabc */
          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;

            LABC[Gab] = global_dpd_->dpd_block_matrix(I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);

            for(ab=0; ab < I_ovvv.params->coltot[Gab]; ab++) {
              A = I_ovvv.params->colorb[Gab][ab][0];
              Ga = I_ovvv.params->rsym[A];
              a = A - bvir_off_[Ga];
              B = I_ovvv.params->colorb[Gab][ab][1];
              Gb = I_ovvv.params->ssym[B];
              b = B - bvir_off_[Gb];

              Gbc = Gb ^ Gc;
              Gac = Ga ^ Gc;

              for(c=0; c < nbvirpi_[Gc]; c++) {
                C = bvir_off_[Gc] + c;

                /* Copy W into labda_ijkabc */
                LABC[Gab][ab][c] = WABC[Gab][ab][c];

                /* Build the rest of the denominator and compute labda_ijkabc */
                denom = dijk;

                if(moF0b_->rowspi()[Ga])
                    denom -= moF0b_->get(Ga, a + nboccpi_[Ga], a + nboccpi_[Ga]);
                if(moF0b_->rowspi()[Gb])
                    denom -= moF0b_->get(Gb, b + nboccpi_[Gb], b + nboccpi_[Gb]);
                if(moF0b_->rowspi()[Gc])
                    denom -= moF0b_->get(Gc, c + nboccpi_[Gc], c + nboccpi_[Gc]);

//                fprintf(outfile, "\td[%d][%d][%d][%d][%d][%d] = %20.10f\n", i, j, k, a + naoccpi_[Ga], b + naoccpi_[Gb], c + naoccpi_[Gc], denom);

                LABC[Gab][ab][c] /= denom;

              } /* c */
            } /* ab */
          } /* Gab */

          for(Gab=0; Gab < nirrep_; Gab++) {
            Gc = Gab ^ Gijk;
            ET_bbb += dot_block(LABC[Gab], WABC[Gab], I_ovvv.params->coltot[Gab], nbvirpi_[Gc], 1.0/6.0);
            global_dpd_->free_dpd_block(WABC[Gab], I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
            global_dpd_->free_dpd_block(LABC[Gab], I_ovvv.params->coltot[Gab], nbvirpi_[Gc]);
          }

            } /* I >= J >= K */

          } /* k */
        } /* j */
      } /* i */

        } /* Gk */
      } /* Gj */
    } /* Gi */

    free(WABC);
    free(LABC);
    free(WBCA);
    free(WACB);

    for(h=0; h < nirrep_; h++) {
      global_dpd_->buf4_mat_irrep_close(&L, h);
      global_dpd_->buf4_mat_irrep_close(&I_ooov, h);
    }

    global_dpd_->buf4_close(&L);
    global_dpd_->buf4_close(&I_ovvv);
    global_dpd_->buf4_close(&I_ooov);

    psio_->close(PSIF_LIBTRANS_DPD, 1);

    return ET_bbb;

}

}} //End namespaces
