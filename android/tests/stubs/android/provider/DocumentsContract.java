package android.provider;
import android.net.Uri;
public final class DocumentsContract {
    public static String getTreeDocumentId(Uri tree) { return tree.id; }
    public static Uri buildChildDocumentsUriUsingTree(Uri tree, String id) { return new Uri(id); }
    public static Uri buildDocumentUriUsingTree(Uri tree, String id) { return new Uri(id); }
    public static final class Document {
        public static final String COLUMN_DOCUMENT_ID = "document_id";
        public static final String COLUMN_DISPLAY_NAME = "display_name";
        public static final String COLUMN_MIME_TYPE = "mime_type";
        public static final String MIME_TYPE_DIR = "vnd.android.document/directory";
    }
}
