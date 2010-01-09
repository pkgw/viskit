#include <viskit/dataset.h>
#include <stdio.h>

int
main (int argc, char **argv)
{
    Dataset *dsin, *dsout;
    GError *err = NULL;
    GSList *items, *iter;

    if (argc != 3) {
	fprintf (stderr, "Usage: %s <input dataset> <output dataset>\n",
		 argv[0]);
	return 1;
    }

    if ((dsin = ds_open (argv[1], IO_MODE_READ, 0, &err)) == NULL) {
	fprintf (stderr, "Error opening %s for input: %s\n", argv[1],
		 err->message);
	return 1;
    }

    if ((dsout = ds_open (argv[2], IO_MODE_WRITE, DS_OFLAGS_CREATE_OK |
			  DS_OFLAGS_APPEND, &err)) == NULL) {
	fprintf (stderr, "Error opening %s for output: %s\n", argv[2],
		 err->message);
	return 1;
    }

    if (ds_list_items (dsin, &items, &err)) {
	fprintf (stderr, "Error listing %s: %s\n", argv[1], err->message);
	return 1;
    }

    for (iter = items; iter; iter = iter->next) {
	DSItemInfo *dsii;

	if (ds_probe_item (dsin, (gchar *) iter->data, &dsii, &err)) {
	    fprintf (stderr, "Error probing item \"%s\" in %s: %s\n",
		     (gchar *) iter->data, argv[1], err->message);
	    return 1;
	}

	if (!dsii->is_large) {
	    if (ds_has_item (dsout, dsii->name)) {
		DSItemInfo *outii;
		size_t nbytes;

		if (ds_probe_item (dsout, dsii->name, &outii, &err)) {
		    fprintf (stderr, "Error probing small item \"%s\" in %s: %s\n",
			     dsii->name, argv[2], err->message);
		    return 1;
		}

		if (outii->is_large) {
		    fprintf (stderr, "Error: existing item \"%s\" in destination "
			     "%s is a large item but is a small one in source %s.\n",
			     dsii->name, argv[2], argv[1]);
		    return 1;
		}

		if (outii->type != dsii->type || outii->nvals != dsii->nvals) {
		    fprintf (stderr, "Error: existing small item \"%s\" in destination "
			     "%s is of different type and/or size than in source %s.\n",
			     dsii->name, argv[2], argv[1]);
		    return 1;
		}

		nbytes = ds_type_sizes[dsii->type] * dsii->nvals;

		if (memcmp (dsii->small.i8, outii->small.i8, nbytes)) {
		    fprintf (stderr, "Error: existing small item \"%s\" in destination "
			     "%s does not have the same value as the one in source %s.\n",
			     dsii->name, argv[2], argv[1]);
		    return 1;
		}

		ds_item_info_free (outii);
	    } else {
		DSError dserr = ds_set_small_item (dsout, dsii->name, dsii->type,
						   dsii->nvals, dsii->small.i8, TRUE);

		if (dserr) {
		    fprintf (stderr, "Error copying small item \"%s\" from %s to %s: "
			     "%s\n", dsii->name, argv[1], argv[2],
			     ds_error_describe (dserr));
		    return 1;
		}
	    }
	} else {
	    IOStream *ioin, *ioout;

	    if ((ioin = ds_open_large_item (dsin, dsii->name, IO_MODE_READ,
					    0, &err)) == NULL) {
		fprintf (stderr, "Error opening item \"%s\" in %s for input: %s\n",
			 dsii->name, argv[1], err->message);
		return 1;
	    }

	    if ((ioout = ds_open_large_item (dsout, dsii->name, IO_MODE_WRITE,
					     DS_OFLAGS_CREATE_OK | DS_OFLAGS_APPEND,
					     &err)) == NULL) {
		fprintf (stderr, "Error opening item \"%s\" in %s for output: %s\n",
			 dsii->name, argv[2], err->message);
		return 1;
	    }

	    if (io_pipe (ioin, ioout, &err)) {
		fprintf (stderr, "Error copying large item \"%s\" from %s to %s: %s\n",
			 dsii->name, argv[1], argv[2], err->message);
		return 1;
	    }

	    if (io_close_and_free (ioin, &err)) {
		fprintf (stderr, "Error closing item \"%s\" in %s: %s\n",
			 dsii->name, argv[1], err->message);
		return 1;
	    }

	    if (io_close_and_free (ioout, &err)) {
		fprintf (stderr, "Error closing item \"%s\" in %s: %s\n",
			 dsii->name, argv[2], err->message);
		return 1;
	    }
	}

	ds_item_info_free (dsii);
    }

    if (ds_close (dsout, &err)) {
	fprintf (stderr, "Error closing %s: %s\n", argv[2], err->message);
	return 1;
    }

    if (ds_close (dsin, &err)) {
	fprintf (stderr, "Error closing %s: %s\n", argv[1], err->message);
	return 1;
    }

    g_slist_foreach (items, (GFunc) g_free, NULL);
    g_slist_free (items);
    return 0;
}
