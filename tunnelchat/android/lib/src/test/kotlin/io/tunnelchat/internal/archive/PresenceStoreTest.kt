package io.tunnelchat.internal.archive

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class PresenceStoreTest {
    private lateinit var db: TunnelchatDatabase
    private lateinit var store: PresenceStore

    @Before
    fun setUp() {
        db = TunnelchatDatabase.inMemory(ApplicationProvider.getApplicationContext())
        store = PresenceStore(db.presence())
    }

    @After
    fun tearDown() = db.close()

    @Test
    fun upsert_overwritesExisting() = runTest {
        store.upsert(SenderId(1u), Presence.Online, 100L)
        store.upsert(SenderId(1u), Presence.Stale, 200L)
        val snap = store.get(SenderId(1u))!!
        assertEquals(Presence.Stale, snap.presence)
        assertEquals(200L, snap.lastHeardMs)
        assertEquals(1, store.all().size)
    }

    @Test
    fun get_returnsNullForUnknownSender() = runTest {
        assertNull(store.get(SenderId(7u)))
    }

    @Test
    fun all_returnsEveryRow() = runTest {
        store.upsert(SenderId(1u), Presence.Online, 100L)
        store.upsert(SenderId(2u), Presence.Offline, 100L)
        store.upsert(SenderId(3u), Presence.Stale, 100L)
        assertEquals(3, store.all().size)
    }
}
