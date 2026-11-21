package org.nightmarecreatures.port;

import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONException;
import org.json.JSONObject;
import org.libsdl.app.SDLActivity;

import java.util.Locale;

public class NightmareActivity extends SDLActivity {
    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            return cursor != null && cursor.moveToFirst() ? cursor.getString(0) : null;
        }
    }

    // CUE paths are filenames. A document provider may give each BIN an unrelated opaque URI.
    public String resolveTrack(String sheet, String filename) throws JSONException {
        SharedPreferences prefs = getSharedPreferences("disc-files", MODE_PRIVATE);
        while (filename.startsWith("./")) filename = filename.substring(2);
        return new JSONObject(prefs.getString(sheet, "{}")).optString(filename, null);
    }

    private void keepReadAccess(Intent data, Uri uri) {
        if ((data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                Log.w("NightmareCreatures", "Disc provider granted access for this session only", e);
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null) {
            ClipData clip = data.getClipData();
            if (clip == null) {
                if (data.getData() != null) keepReadAccess(data, data.getData());
            } else {
                Uri sheet = null;
                JSONObject tracks = new JSONObject();
                try {
                    for (int i = 0; i < clip.getItemCount(); i++) {
                        Uri uri = clip.getItemAt(i).getUri();
                        String name = displayName(uri);
                        if (name == null) throw new IllegalArgumentException();
                        if (name.toLowerCase(Locale.ROOT).endsWith(".cue")) {
                            if (sheet != null) throw new IllegalArgumentException();
                            sheet = uri;
                        } else {
                            if (tracks.has(name)) throw new IllegalArgumentException();
                            tracks.put(name, uri.toString());
                        }
                    }
                    if (clip.getItemCount() > 1 && sheet == null) throw new IllegalArgumentException();
                    for (int i = 0; i < clip.getItemCount(); i++) {
                        keepReadAccess(data, clip.getItemAt(i).getUri());
                    }
                    if (sheet != null && tracks.length() != 0) {
                        getSharedPreferences("disc-files", MODE_PRIVATE).edit()
                            .putString(sheet.toString(), tracks.toString()).apply();
                    }
                    // Forward the entry document to SDL's single-disc callback.
                    data.setData(sheet != null ? sheet : clip.getItemAt(0).getUri());
                } catch (JSONException | RuntimeException e) {
                    Toast.makeText(this, "Choose one CUE and its matching BIN files together.",
                            Toast.LENGTH_LONG).show();
                    data = null;
                    resultCode = RESULT_CANCELED;
                }
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
