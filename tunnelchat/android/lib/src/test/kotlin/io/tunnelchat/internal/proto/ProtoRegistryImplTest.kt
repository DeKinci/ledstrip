package io.tunnelchat.internal.proto

import io.tunnelchat.builtin.Echo
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class ProtoRegistryImplTest {

    @Test
    fun register_and_lookup_app_schema() {
        val r = ProtoRegistryImpl()
        r.register(256u, Echo.parser())
        assertTrue(r.isRegistered(256u))
        assertNotNull(r.parserFor(256u))
    }

    @Test
    fun unregister_drops_schema() {
        val r = ProtoRegistryImpl()
        r.register(300u, Echo.parser())
        r.unregister(300u)
        assertFalse(r.isRegistered(300u))
        assertNull(r.parserFor(300u))
    }

    @Test
    fun rejects_reserved_ids() {
        val r = ProtoRegistryImpl()
        for (id in intArrayOf(0, 1, 2, 255)) {
            try {
                r.register(id.toUShort(), Echo.parser())
                fail("should reject reserved id=$id")
            } catch (_: IllegalArgumentException) { /* expected */ }
        }
    }

    @Test
    fun first_app_id_accepted() {
        val r = ProtoRegistryImpl()
        r.register(ProtoRegistryImpl.RESERVED_MAX_EXCL.toUShort(), Echo.parser())
        assertTrue(r.isRegistered(ProtoRegistryImpl.RESERVED_MAX_EXCL.toUShort()))
    }
}
