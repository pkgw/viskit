#include <viskit/dataset.h>
#include <stdio.h>
#include <errno.h>

/* Some ATA datasets are missing the flags for their final visibility
 * record. (This happens occasionally when the data catcher is shut
 * down by a signal.) MIRIAD doesn't provide good software hooks for
 * correcting this situation, so let's do it here.
 *
 * We're actually not using the MaskItem interface since it doesn't
 * support the kind of read-write operations that we'd like to do
 * here. (We could do it in a streaming way, but that'd require reading
 * through all of the flags, which isn't necessary here.)
 */

#define NCHAN 1024
#define BUFSZ (NCHAN / 31 + 1)

int
main (int argc, char **argv)
{
    Dataset *vis;
    GError *err = NULL;
    gint64 ncorr, nflags, nrecsmissing, nbitslastrec;
    DSItemInfo *finfo;
    gchar *flagsfn;
    FILE *flags;
    guint32 buf[BUFSZ], tmp;
    int i, j;
    size_t ntowrite;

    if (argc != 2) {
	fprintf (stderr, "Usage: %s <dataset>\n",
		 argv[0]);
	return 1;
    }

    if ((vis = ds_open (argv[1], IO_MODE_READ, 0, &err)) == NULL) {
	fprintf (stderr, "Error opening %s: %s\n", argv[1], err->message);
	return 1;
    }

    if (!ds_has_item (vis, "flags")) {
	fprintf (stderr, "Error: dataset %s does not contain a \"flags\" item.\n",
		 argv[1]);
	return 1;
    }

    if (ds_get_item_i64 (vis, "ncorr", &ncorr)) {
	fprintf (stderr, "Error: cannot retrieve value of \"ncorr\" item.\n");
	return 1;
    }

    if (ds_probe_item (vis, "flags", &finfo, &err)) {
	fprintf (stderr, "Error probing \"flags\" item: %s\n", err->message);
	return 1;
    }

    nflags = finfo->nvals * 31; /* 31 of 32 bits per int used, just to be annoying */
    nrecsmissing = (ncorr + NCHAN - 1 - nflags) / NCHAN;
    nbitslastrec = nflags + NCHAN * nrecsmissing - ncorr;

    printf ("              Number of correlations: %"G_GINT64_FORMAT"\n", ncorr);
    printf ("         Max. number of flag entries: %"G_GINT64_FORMAT"\n", nflags);
    printf ("           Number of missing records: %"G_GINT64_FORMAT"\n", nrecsmissing);
    printf ("Num. invalid bits in last flag datum: %"G_GINT64_FORMAT"\n", nbitslastrec);

    if (nrecsmissing == 0)
	return 0;

    if (nrecsmissing != 1) {
	fprintf (stderr, "Error: expect exactly 1 missing record.\n");
	return 1;
    }

    if (nbitslastrec > 30) {
	fprintf (stderr, "Error: expect between 0 and 30 invalid bits in last record.\n");
	return 1;
    }

    if (ds_close (vis, &err)) {
	fprintf (stderr, "Error closing dataset \"%s\": %s\n", argv[1], err->message);
	return 1;
    }

    /* Initialize data to write to flags file -- this may be modified below
     * if there are extra bits at the end of the flags file that we need to
     * preserve. We zero out the middle (DC) channel because that's what the
     * ATA does. To do this we need to take into account the extra bits at
     * the end of the existing file.
     *
     * Here we change the semantics of nbitslastrec to be the number of extra
     * bits from the last record that we need to preserve.
     *
     * number of bits to write: one per chan, then the extra ones for the last int.
     * number of ints: number of bits / 31, rounded up. */

    if (nbitslastrec > 0)
	nbitslastrec = 31 - nbitslastrec;

    ntowrite = (NCHAN + nbitslastrec + 30) / 31;

    for (i = 0; i < ntowrite; i++)
	buf[i] = 0x7FFFFFFF;

    i = NCHAN / 2 + nbitslastrec; /* bit offset of middle channel */
    j = i / 31; /* int offset */
    i -= 31 * j; /* bit offset within that int */

    /*
    printf ("boffset: %"G_GINT64_FORMAT"\n", NCHAN / 2 + nbitslastrec);
    printf ("int ofs: %d\n", j);
    printf ("bit ofs: %d\n", i);
    */

    buf[j] &= ~(1 << i);

    /* Now we begin the surgery. */

    flagsfn = g_strdup_printf ("%s/flags", argv[1]);
    if ((flags = fopen (flagsfn, "r+")) == NULL) {
	fprintf (stderr, "Error opening flags file \"%s\" for modification: %s\n",
		 flagsfn, g_strerror (errno));
	return 1;
    }

    if (nbitslastrec != 0) {
	if (fseek (flags, -4, SEEK_END) < 0) {
	    fprintf (stderr, "Error seeking to near-end of flags file \"%s\": %s\n",
		     flagsfn, g_strerror (errno));
	    return 1;
	}

	if (fread (&tmp, 4, 1, flags) != 1) {
	    fprintf (stderr, "Error reading end of flags file \"%s\": %s\n",
		     flagsfn, g_strerror (errno));
	    return 1;
	}

	/* Our bits are all 1s over here so we can just apply the
	 * previous flags with an &. Breaks if NCHAN < 64, but it's not. Also
	 * must pay attention to endianness. */
	buf[0] &= GINT32_FROM_BE (tmp);

	/* Back up again to rewrite that last int32. */
	if (fseek (flags, -4, SEEK_END) < 0) {
	    fprintf (stderr, "Error seeking to near-end of flags file \"%s\": %s\n",
		     flagsfn, g_strerror (errno));
	    return 1;
	}
    }

    for (i = 0; i < ntowrite; i++)
	buf[i] = GINT32_FROM_BE (buf[i]);

    if (fwrite (buf, 4, ntowrite, flags) != ntowrite) {
	fprintf (stderr, "Error modifying flags file \"%s\": %s\n",
		 flagsfn, g_strerror (errno));
	return 1;
    }

    if (fclose (flags)) {
	fprintf (stderr, "Error closing flags file \"%s\": %s\n",
		 flagsfn, g_strerror (errno));
	return 1;
    }

    g_free (flagsfn);
    return 0;
}
