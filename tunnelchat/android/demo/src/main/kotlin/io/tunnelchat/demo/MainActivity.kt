package io.tunnelchat.demo

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** Minimal placeholder Activity. Phase 11 fills in the real manual-test UI. */
class MainActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(TextView(this).apply { text = "tunnelchat demo (scaffold)" })
    }
}
