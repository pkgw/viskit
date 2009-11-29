#include <stdio.h>
#include <viskit/dataset.h>

int
main (int argc, char **argv)
{
    Dataset *ds;
    DSItemInfo *dii;
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
	gchar *name = (gchar *) iter->data;
	gint64 ival;
	gdouble dval;

	dii = ds_probe_item (ds, name, &err);

	if (dii == NULL) {
	    fprintf (stderr, "Error probing item \"%s\" in \"%s\": %s\n",
		     name, argv[1], err->message);
	    return 1;
	}

	printf ("%-10s", dii->name);

	if (dii->is_large)
	    printf ("large");
	else
	    printf ("small");

	printf (" %-12s %10d vals", ds_type_names[dii->type], dii->nvals);

	if (dii->nvals == 1) {
	    switch (dii->type) {
	    case DST_I8:
	    case DST_I16:
	    case DST_I32:
	    case DST_I64:
		if (ds_get_item_i64 (ds, name, &ival))
		    printf (" = [unreadable?!]");
		else
		    printf (" = %" G_GINT64_FORMAT, ival);
		break;
	    case DST_F32:
	    case DST_F64:
		if (ds_get_item_f64 (ds, name, &dval))
		    printf (" = [unreadable?!]");
		else
		    printf (" = %f", dval);
		break;
	    default:
		break;
	    }
	} else if (!dii->is_large && dii->type == DST_I8) {
	    gchar *s = ds_get_item_small_string (ds, name);

	    if (s == NULL)
		printf (" = [unstringable?!]");
	    else {
		printf (" = \"%s\"", s);
		g_free (s);
	    }
	}

	printf ("\n");
	g_free (iter->data);
	ds_item_info_free (dii);
    }

    g_slist_free (items);
    ds_close (ds);
    return 0;
}
