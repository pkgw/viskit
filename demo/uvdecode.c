#include <stdio.h>
#include <glib.h>
#include <viskit/uvreader.h>

int
main (int argc, char **argv)
{
    Dataset *ds;
    UVReader *uvr;
    UVEntryType uvet;
    UVVariable *var;
    gchar *uvdata, *buf;
    GError *err = NULL;
    guint nrec = 0;

    if (argc != 2) {
	fprintf (stderr, "Usage: %s <uvname>\n", argv[0]);
	return 1;
    }

    if ((ds = ds_open (argv[1], DSM_READ, &err)) == NULL) {
	fprintf (stderr, "Error opening \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    uvr = uvr_alloc ();
    if (uvr_prep (uvr, ds, &err)) {
	fprintf (stderr, "Error opening UV stream of dataset \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    while (TRUE) {
	uvet = uvr_next (uvr, &uvdata, &err);

	switch (uvet) {
	case UVET_ERROR:
	    fprintf (stderr, "Error reading UV stream of dataset \"%s\": %s\n",
		     argv[1], err->message);
	    return 1;
	case UVET_SIZE:
	    var = (UVVariable *) uvdata;
	    printf ("%s.nval = %li\n", var->name, (long int) var->nvals);
	    break;
	case UVET_DATA:
	    var = (UVVariable *) uvdata;
	    buf = ds_type_format (var->data, var->type, var->nvals);
	    printf ("%s.data = %s\n", var->name, buf);
	    g_free (buf);
	    break;
	case UVET_EOR:
	    printf ("-- EOR (%u) --\n", nrec);
	    nrec++;
	    break;
	case UVET_EOS:
	    printf ("-- EOS --\n");
	    break;
	}

	if (uvet == UVET_EOS)
	    break;
    }

    uvr_free (uvr);
    ds_close (ds);
    return 0;
}
