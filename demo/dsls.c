#include <stdio.h>
#include <viskit/dataset.h>

int
main (int argc, char **argv)
{
    Dataset *ds;
    GSList *items, *iter;
    GError *err = NULL;

    if (argc != 2) {
	fprintf (stderr, "Usage: %s <dsname>\n", argv[0]);
	return 1;
    }

    if ((ds = ds_open (argv[1], DSM_READ, &err)) == NULL) {
	fprintf (stderr, "Error opening \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    if ((items = ds_list_items (ds, &err)) == NULL) {
	fprintf (stderr, "Error scanning items in \"%s\": %s\n",
		 argv[1], err->message);
	return 1;
    }

    for (iter = items; iter; iter = iter->next) {
	printf ("%s\n", (char *) iter->data);
	g_free (iter->data);
    }

    g_slist_free (items);
    ds_close (ds);
    return 0;
}
