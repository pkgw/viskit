#include <stdio.h>
#include <glib.h>

#include <viskit/uvio.h>

int
main (int argc, char **argv)
{
    Dataset *dsin, *dsout;
    UVIO *uvin, *uvout;
    UVEntryType uvet;
    UVVariable *var;
    gpointer uvdata;
    GError *err = NULL;
    guint nrec = 0;

    if (argc != 3) {
	fprintf (stderr, "Usage: %s <uvinput> <uvoutput>\n", argv[0]);
	return 1;
    }

    /* Opening for reading */

    if ((dsin = ds_open (argv[1], IO_MODE_READ, 0, &err)) == NULL) {
	fprintf (stderr, "Error opening \"%s\" for reading: %s\n",
		 argv[1], err->message);
	return 1;
    }

    uvin = uvio_alloc ();
    if (uvio_open (uvin, dsin, IO_MODE_READ, 0, &err)) {
	fprintf (stderr, "Error opening UV stream of dataset \"%s\" for reading: %s\n",
		 argv[1], err->message);
	return 1;
    }

    /* Opening for writing */

    if ((dsout = ds_open (argv[2], IO_MODE_WRITE,
			  DS_OFLAGS_CREATE_OK | DS_OFLAGS_APPEND, &err)) == NULL) {
	fprintf (stderr, "Error opening \"%s\" for writing: %s\n",
		 argv[2], err->message);
	return 1;
    }

    uvout = uvio_alloc ();
    if (uvio_open (uvout, dsout, IO_MODE_WRITE,
		   DS_OFLAGS_CREATE_OK | DS_OFLAGS_APPEND, &err)) {
	fprintf (stderr, "Error opening UV stream of dataset \"%s\" for writing: %s\n",
		 argv[2], err->message);
	return 1;
    }

    /* Pipe the data! */

    while (TRUE) {
	uvet = uvio_read_next (uvin, &uvdata, &err);

	switch (uvet) {
	case UVET_ERROR:
	    fprintf (stderr, "Error reading UV stream of dataset \"%s\": %s\n",
		     argv[1], err->message);
	    return 1;
	case UVET_DATA:
	    var = (UVVariable *) uvdata;
	    if (uvio_write_var (uvout, var->name, var->type, var->nvals,
				var->data, &err)) {
		fprintf (stderr, "Error writing UV stream of dataset \"%s\": %s\n",
			 argv[2], err->message);
		return 1;
	    }
	    break;
	case UVET_EOR:
	    if (uvio_write_end_record (uvout, &err)) {
		fprintf (stderr, "Error writing UV stream of dataset \"%s\": %s\n",
			 argv[2], err->message);
		return 1;
	    }

	    if (nrec == 0 && uvio_update_vartable (uvout, &err)) {
		fprintf (stderr, "Error writing UV stream of dataset \"%s\": %s\n",
			 argv[2], err->message);
		return 1;
	    }

	    nrec++;
	    break;
	case UVET_EOS:
	case UVET_SIZE:
	    break;
	}

	if (uvet == UVET_EOS)
	    break;
    }

    if (uvio_close (uvout, &err)) {
	fprintf (stderr, "Error closing UV stream of dataset \"%s\": %s\n",
		 argv[2], err->message);
	return 1;
    }

    uvio_free (uvout);

    if (ds_close (dsout, &err)) {
	fprintf (stderr, "Error closing dataset \"%s\": %s\n",
		 argv[2], err->message);
	return 1;
    }

    if (uvio_close (uvin, &err)) {
	fprintf (stderr, "Error closing UV stream of dataset \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    uvio_free (uvin);

    if (ds_close (dsin, &err)) {
	fprintf (stderr, "Error closing dataset \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    return 0;
}
