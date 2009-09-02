#include <stdio.h>
#include <viskit/uvreader.h>

int
main (int argc, char **argv)
{
    Dataset *ds;
    UVReader *uvr;
    UVEntryType uvet;
    gchar *uvdata;
    GError *err = NULL;

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
	    printf ("size\n");
	    break;
	case UVET_DATA:
	    printf ("data\n");
	    break;
	case UVET_EOR:
	    printf ("EOR\n");
	    break;
	case UVET_EOS:
	    printf ("EOS\n");
	    break;
	}

	if (uvet == UVET_EOS)
	    break;
    }

    uvr_free (uvr);
    ds_close (ds);
    return 0;
}
