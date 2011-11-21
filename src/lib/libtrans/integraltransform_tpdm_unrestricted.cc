#include "integraltransform.h"
#include <libchkpt/chkpt.hpp>
#include <libpsio/psio.hpp>
#include <libciomr/libciomr.h>
#include <libiwl/iwl.hpp>
#include <libqt/qt.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include "psifiles.h"
#include "ccfiles.h"
#include "mospace.h"
#define EXTERN
#include <libdpd/dpd.gbl>

using namespace psi;

void
IntegralTransform::backtransform_tpdm_unrestricted()
{
    check_initialized();

    // This can be safely called - it returns immediately if the MO TPDM is already sorted
    presort_mo_tpdm_unrestricted();
    // Grab the transformation coefficients
    double ***ca = _aMOCoefficients[MOSPACE_ALL];
    double ***cb = _bMOCoefficients[MOSPACE_ALL];

    dpdbuf4 J1, J2, K;

    // Grab control of DPD for now, but store the active number to restore it later
    int currentActiveDPD = psi::dpd_default;
    dpd_set_default(_myDPDNum);

    int nBuckets;
    int thisBucketRows;
    size_t rowsPerBucket;
    size_t rowsLeft;
    size_t memFree;

    double **TMP = block_matrix(_nso, _nso);

    /*** first half transformation ***/

    if(_print) {
        fprintf(outfile, "\tStarting first half-transformation.\n");
        fflush(outfile);
    }

    _psio->open(PSIF_TPDM_PRESORT, PSIO_OPEN_OLD);
    _psio->open(PSIF_TPDM_HALFTRANS, PSIO_OPEN_NEW);

    /*
     * (AA|AA) & (AA|aa) -> (AA|nn)
     */
    dpd_buf4_init(&J1, PSIF_TPDM_PRESORT, 0, DPD_ID("[A>=A]+"), DPD_ID("[A,A]"),
                  DPD_ID("[A>=A]+"), DPD_ID("[A>=A]+"), 0, "MO TPDM (AA|AA)");
    dpd_buf4_init(&J2, PSIF_TPDM_PRESORT, 0, DPD_ID("[A>=A]+"), DPD_ID("[a,a]"),
                  DPD_ID("[A>=A]+"), DPD_ID("[a>=a]+"), 0, "MO TPDM (AA|aa)");
    dpd_buf4_init(&K, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[A>=A]+"), DPD_ID("[n,n]"),
                  DPD_ID("[A>=A]+"), DPD_ID("[n>=n]+"), 0, "Half-Transformed TPDM (AA|nn)");

    for(int h=0; h < _nirreps; h++) {
        if(J1.params->coltot[h] && J1.params->rowtot[h]) {
            memFree = static_cast<size_t>(dpd_memfree() - J1.params->coltot[h] - K.params->coltot[h]);
            rowsPerBucket = memFree/(2 * J1.params->coltot[h]);
            if(rowsPerBucket > J1.params->rowtot[h]) rowsPerBucket = (size_t) J1.params->rowtot[h];
            nBuckets = static_cast<int>(ceil(static_cast<double>(J1.params->rowtot[h])/
                                        static_cast<double>(rowsPerBucket)));
            rowsLeft = static_cast<size_t>(J1.params->rowtot[h] % rowsPerBucket);
        }else{
            nBuckets = 0;
            rowsPerBucket = 0;
            rowsLeft = 0;
        }

        if(_print > 1) {
            fprintf(outfile, "\th = %d; memfree         = %lu\n", h, memFree);
            fprintf(outfile, "\th = %d; rows_per_bucket = %lu\n", h, rowsPerBucket);
            fprintf(outfile, "\th = %d; rows_left       = %lu\n", h, rowsLeft);
            fprintf(outfile, "\th = %d; nbuckets        = %d\n", h, nBuckets);
            fflush(outfile);
        }

        dpd_buf4_mat_irrep_init_block(&K, h, rowsPerBucket);
        for(int n=0; n < nBuckets; n++){

            if(nBuckets == 1)
                thisBucketRows = rowsPerBucket;
            else
                thisBucketRows = (n < nBuckets-1) ? rowsPerBucket : rowsLeft;

            dpd_buf4_mat_irrep_init_block(&J1, h, rowsPerBucket);
            dpd_buf4_mat_irrep_rd_block(&J1, h, n*rowsPerBucket, thisBucketRows);
            for(int pq=0; pq < thisBucketRows; pq++) {
                for(int Gr=0; Gr < _nirreps; Gr++) {
                    // Transform ( A A | A A ) -> ( A A | A n )
                    int Gs = h^Gr;
                    int nrows = _sopi[Gr];
                    int ncols = _mopi[Gs];
                    int nlinks = _mopi[Gs];
                    int rs = J1.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 't', nrows, ncols, nlinks, 1.0, &J1.matrix[h][pq][rs],
                                nlinks, ca[Gs][0], ncols, 0.0, TMP[0], _nso);

                    // Transform ( A A | A n ) -> ( A A | n n )
                    nrows = _sopi[Gr];
                    ncols = _sopi[Gs];
                    nlinks = _mopi[Gr];
                    rs = K.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 'n', nrows, ncols, nlinks, 1.0, ca[Gr][0], nrows,
                                TMP[0], _nso, 0.0, &K.matrix[h][pq][rs], ncols);
                } /* Gr */
            } /* pq */
            dpd_buf4_mat_irrep_close_block(&J1, h, rowsPerBucket);

            dpd_buf4_mat_irrep_init_block(&J2, h, rowsPerBucket);
            dpd_buf4_mat_irrep_rd_block(&J2, h, n*rowsPerBucket, thisBucketRows);
            for(int pq=0; pq < thisBucketRows; pq++) {
                for(int Gr=0; Gr < _nirreps; Gr++) {
                    // Transform ( A A | a a ) -> ( A A | a n )
                    int Gs = h^Gr;
                    int nrows = _sopi[Gr];
                    int ncols = _mopi[Gs];
                    int nlinks = _mopi[Gs];
                    int rs = J2.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 't', nrows, ncols, nlinks, 1.0, &J2.matrix[h][pq][rs],
                                nlinks, cb[Gs][0], ncols, 0.0, TMP[0], _nso);

                    // Transform ( A A | a n ) -> ( A A | n n )
                    nrows = _sopi[Gr];
                    ncols = _sopi[Gs];
                    nlinks = _mopi[Gr];
                    rs = K.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 'n', nrows, ncols, nlinks, 1.0, cb[Gr][0], nrows,
                                TMP[0], _nso, 1.0, &K.matrix[h][pq][rs], ncols);
                } /* Gr */
            } /* pq */
            dpd_buf4_mat_irrep_wrt_block(&K, h, n*rowsPerBucket, thisBucketRows);
            dpd_buf4_mat_irrep_close_block(&J2, h, rowsPerBucket);
        }
        dpd_buf4_mat_irrep_close_block(&K, h, rowsPerBucket);
    }
    dpd_buf4_close(&K);
    dpd_buf4_close(&J1);
    dpd_buf4_close(&J2);

    /*
     * (aa|aa) -> (aa|nn)
     */
    dpd_buf4_init(&J1, PSIF_TPDM_PRESORT, 0, DPD_ID("[a>=a]+"), DPD_ID("[a,a]"),
                  DPD_ID("[a>=a]+"), DPD_ID("[a>=a]+"), 0, "MO TPDM (aa|aa)");
    dpd_buf4_init(&K, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[a>=a]+"), DPD_ID("[n,n]"),
                  DPD_ID("[a>=a]+"), DPD_ID("[n>=n]+"), 0, "Half-Transformed TPDM (aa|nn)");

    for(int h=0; h < _nirreps; h++) {
        if(J1.params->coltot[h] && J1.params->rowtot[h]) {
            memFree = static_cast<size_t>(dpd_memfree() - J1.params->coltot[h] - K.params->coltot[h]);
            rowsPerBucket = memFree/(2 * J1.params->coltot[h]);
            if(rowsPerBucket > J1.params->rowtot[h]) rowsPerBucket = (size_t) J1.params->rowtot[h];
            nBuckets = static_cast<int>(ceil(static_cast<double>(J1.params->rowtot[h])/
                                        static_cast<double>(rowsPerBucket)));
            rowsLeft = static_cast<size_t>(J1.params->rowtot[h] % rowsPerBucket);
        }else{
            nBuckets = 0;
            rowsPerBucket = 0;
            rowsLeft = 0;
        }

        if(_print > 1) {
            fprintf(outfile, "\th = %d; memfree         = %lu\n", h, memFree);
            fprintf(outfile, "\th = %d; rows_per_bucket = %lu\n", h, rowsPerBucket);
            fprintf(outfile, "\th = %d; rows_left       = %lu\n", h, rowsLeft);
            fprintf(outfile, "\th = %d; nbuckets        = %d\n", h, nBuckets);
            fflush(outfile);
        }

        dpd_buf4_mat_irrep_init_block(&K, h, rowsPerBucket);
        for(int n=0; n < nBuckets; n++){

            if(nBuckets == 1)
                thisBucketRows = rowsPerBucket;
            else
                thisBucketRows = (n < nBuckets-1) ? rowsPerBucket : rowsLeft;

            dpd_buf4_mat_irrep_init_block(&J1, h, rowsPerBucket);
            dpd_buf4_mat_irrep_rd_block(&J1, h, n*rowsPerBucket, thisBucketRows);
            for(int pq=0; pq < thisBucketRows; pq++) {
                for(int Gr=0; Gr < _nirreps; Gr++) {
                    // Transform ( a a | a a ) -> ( a a | a n )
                    int Gs = h^Gr;
                    int nrows = _sopi[Gr];
                    int ncols = _mopi[Gs];
                    int nlinks = _mopi[Gs];
                    int rs = J1.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 't', nrows, ncols, nlinks, 1.0, &J1.matrix[h][pq][rs],
                                nlinks, cb[Gs][0], ncols, 0.0, TMP[0], _nso);

                    // Transform ( a a | a n ) -> ( a a | n n )
                    nrows = _sopi[Gr];
                    ncols = _sopi[Gs];
                    nlinks = _mopi[Gr];
                    rs = K.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 'n', nrows, ncols, nlinks, 1.0, cb[Gr][0], nrows,
                                TMP[0], _nso, 0.0, &K.matrix[h][pq][rs], ncols);
                } /* Gr */
            } /* pq */
            dpd_buf4_mat_irrep_wrt_block(&K, h, n*rowsPerBucket, thisBucketRows);
            dpd_buf4_mat_irrep_close_block(&J1, h, rowsPerBucket);
        }
        dpd_buf4_mat_irrep_close_block(&K, h, rowsPerBucket);
    }
    dpd_buf4_close(&K);
    dpd_buf4_close(&J1);

    _psio->close(PSIF_TPDM_PRESORT, _keepDpdMoTpdm);

    if(_print) {
        fprintf(outfile, "\tSorting half-transformed TPDMs.\n");
        fflush(outfile);
    }

    dpd_buf4_init(&K, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[A>=A]+"), DPD_ID("[n>=n]+"),
                  DPD_ID("[A>=A]+"), DPD_ID("[n>=n]+"), 0, "Half-Transformed TPDM (AA|nn)");
    dpd_buf4_sort(&K, PSIF_TPDM_HALFTRANS, rspq, DPD_ID("[n>=n]+"), DPD_ID("[A>=A]+"), "Half-Transformed TPDM (nn|AA)");
    dpd_buf4_close(&K);

    dpd_buf4_init(&K, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[a>=a]+"), DPD_ID("[n>=n]+"),
                  DPD_ID("[a>=a]+"), DPD_ID("[n>=n]+"), 0, "Half-Transformed TPDM (aa|nn)");
    dpd_buf4_sort(&K, PSIF_TPDM_HALFTRANS, rspq, DPD_ID("[n>=n]+"), DPD_ID("[a>=a]+"), "Half-Transformed TPDM (nn|aa)");
    dpd_buf4_close(&K);

    if(_print){
        fprintf(outfile, "\tFirst half integral transformation complete.\n");
        fflush(outfile);
    }

    _psio->open(PSIF_AO_TPDM, PSIO_OPEN_NEW);

    /*
     * (nn|AA) & (nn|aa) -> (nn|nn)
     */
    dpd_buf4_init(&J1, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[n>=n]+"), DPD_ID("[A,A]"),
                  DPD_ID("[n>=n]+"), DPD_ID("[A>=A]+"), 0, "Half-Transformed TPDM (nn|AA)");
    dpd_buf4_init(&J2, PSIF_TPDM_HALFTRANS, 0, DPD_ID("[n>=n]+"), DPD_ID("[a,a]"),
                  DPD_ID("[n>=n]+"), DPD_ID("[a>=a]+"), 0, "Half-Transformed TPDM (nn|aa)");
    dpd_buf4_init(&K, PSIF_AO_TPDM, 0, DPD_ID("[n>=n]+"), DPD_ID("[n,n]"),
                  DPD_ID("[n>=n]+"), DPD_ID("[n>=n]+"), 0, "SO Basis TPDM (nn|nn)");

    for(int h=0; h < _nirreps; h++) {
        if(J1.params->coltot[h] && J1.params->rowtot[h]) {
            memFree = static_cast<size_t>(dpd_memfree() - J1.params->coltot[h] - K.params->coltot[h]);
            rowsPerBucket = memFree/(2 * J1.params->coltot[h]);
            if(rowsPerBucket > J1.params->rowtot[h])
                rowsPerBucket = static_cast<size_t>(J1.params->rowtot[h]);
            nBuckets = static_cast<int>(ceil(static_cast<double>(J1.params->rowtot[h])/
                                        static_cast<double>(rowsPerBucket)));
            rowsLeft = static_cast<size_t>(J1.params->rowtot[h] % rowsPerBucket);
        }else {
            nBuckets = 0;
            rowsPerBucket = 0;
            rowsLeft = 0;
        }

        if(_print > 1) {
            fprintf(outfile, "\th = %d; memfree         = %lu\n", h, memFree);
            fprintf(outfile, "\th = %d; rows_per_bucket = %lu\n", h, rowsPerBucket);
            fprintf(outfile, "\th = %d; rows_left       = %lu\n", h, rowsLeft);
            fprintf(outfile, "\th = %d; nbuckets        = %d\n", h, nBuckets);
            fflush(outfile);
        }

        dpd_buf4_mat_irrep_init_block(&K, h, rowsPerBucket);

        for(int n=0; n < nBuckets; n++) {
            if(nBuckets == 1)
                thisBucketRows = rowsPerBucket;
            else
                thisBucketRows = (n < nBuckets-1) ? rowsPerBucket : rowsLeft;


            dpd_buf4_mat_irrep_init_block(&J1, h, rowsPerBucket);
            dpd_buf4_mat_irrep_rd_block(&J1, h, n*rowsPerBucket, thisBucketRows);
            for(int pq=0; pq < thisBucketRows; pq++) {
                for(int Gr=0; Gr < _nirreps; Gr++) {
                    // Transform ( n n | A A ) -> ( n n | A n )
                    int Gs = h^Gr;
                    int nrows = _sopi[Gr];
                    int ncols = _mopi[Gs];
                    int nlinks = _mopi[Gs];
                    int rs = J1.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 't', nrows, ncols, nlinks, 1.0, &J1.matrix[h][pq][rs],
                                nlinks, ca[Gs][0], ncols, 0.0, TMP[0], _nso);

                    // Transform ( n n | n A ) -> ( n n | n n )
                    nrows = _sopi[Gr];
                    ncols = _sopi[Gs];
                    nlinks = _mopi[Gr];
                    rs = K.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 'n', nrows, ncols, nlinks, 1.0, ca[Gr][0], nrows,
                                TMP[0], _nso, 0.0, &K.matrix[h][pq][rs], ncols);
                } /* Gr */
            } /* pq */
            dpd_buf4_mat_irrep_close_block(&J1, h, rowsPerBucket);

            dpd_buf4_mat_irrep_init_block(&J2, h, rowsPerBucket);
            dpd_buf4_mat_irrep_rd_block(&J2, h, n*rowsPerBucket, thisBucketRows);
            for(int pq=0; pq < thisBucketRows; pq++) {
                int PQ = n * rowsPerBucket + pq; // The absolute pq value
                for(int Gr=0; Gr < _nirreps; Gr++) {
                    // Transform ( n n | a a ) -> ( n n | a n )
                    int Gs = h^Gr;
                    int nrows = _sopi[Gr];
                    int ncols = _mopi[Gs];
                    int nlinks = _mopi[Gs];
                    int rs = J2.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 't', nrows, ncols, nlinks, 1.0, &J2.matrix[h][pq][rs],
                                nlinks, cb[Gs][0], ncols, 0.0, TMP[0], _nso);

                    // Transform ( n n | n a ) -> ( n n | n n )
                    nrows = _sopi[Gr];
                    ncols = _sopi[Gs];
                    nlinks = _mopi[Gr];
                    rs = K.col_offset[h][Gr];
                    if(nrows && ncols && nlinks)
                        C_DGEMM('n', 'n', nrows, ncols, nlinks, 1.0, cb[Gr][0], nrows,
                                TMP[0], _nso, 1.0, &K.matrix[h][pq][rs], ncols);
                } /* Gr */
            } /* pq */
            dpd_buf4_mat_irrep_close_block(&J2, h, rowsPerBucket);
            dpd_buf4_mat_irrep_wrt_block(&K, h, n*rowsPerBucket, thisBucketRows);
        }
        dpd_buf4_mat_irrep_close_block(&K, h, rowsPerBucket);
    }
    dpd_buf4_close(&K);
    dpd_buf4_close(&J1);
    dpd_buf4_close(&J2);

    free_block(TMP);

    _psio->close(PSIF_TPDM_HALFTRANS, _keepHtTpdm);
    _psio->close(PSIF_AO_TPDM, 1);

    // Hand DPD control back to the user
    dpd_set_default(currentActiveDPD);
}

