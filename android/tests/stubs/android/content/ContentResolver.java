package android.content;
import android.database.Cursor;
import android.net.Uri;
import java.io.InputStream;
import java.io.IOException;
/** Host test shim for the two SAF provider calls used by the production importer. */
public abstract class ContentResolver {
    public abstract Cursor query(Uri uri, String[] columns, String selection, String[] args, String order);
    public abstract InputStream openInputStream(Uri uri) throws IOException;
}
