#include <stdio.h>
#include <viskit/dataset.h>

/* DataSet - Set Short String item */

int
main (int argc, char **argv)
{
    Dataset *ds;
    GError *err = NULL;
    DSError dserr;

    if (argc != 4) {
	fprintf (stderr, "Usage: %s <dsname> <itemname> <itemvalue>\n", argv[0]);
	return 1;
    }

    if ((ds = ds_open (argv[1], IO_MODE_READ_WRITE, DS_OFLAGS_APPEND, &err)) == NULL) {
	fprintf (stderr, "Error opening \"%s\": %s\n", argv[1], err->message);
	return 1;
    }

    dserr = ds_set_small_item_string (ds, argv[2], argv[3], TRUE);
    if (dserr != DS_ERROR_NO_ERROR) {
	fprintf (stderr, "Error setting item \"%s\": %s\n", argv[2],
		 ds_error_describe (dserr));
	return 1;
    }

    if (ds_close (ds, &err)) {
	fprintf (stderr, "Error writing \"%s\": %s\n", argv[1], err->message);
	return 1;
    }

    return 0;
}
