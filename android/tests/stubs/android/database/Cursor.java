package android.database;
public interface Cursor extends AutoCloseable {
    boolean moveToNext();
    String getString(int column);
    void close();
}
