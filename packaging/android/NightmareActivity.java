package org.nightmarecreatures.port;

import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import org.libsdl.app.SDLActivity;

public class NightmareActivity extends SDLActivity {
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null && data.getData() != null
                && (data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            Uri uri = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                Log.w("NightmareCreatures", "Disc provider granted access for this session only", e);
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
