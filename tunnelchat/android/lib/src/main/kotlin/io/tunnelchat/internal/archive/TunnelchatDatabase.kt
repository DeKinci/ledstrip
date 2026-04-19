package io.tunnelchat.internal.archive

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

@Database(
    entities = [MessageRow::class, PresenceRow::class, StatsRow::class, LogRow::class],
    version = 1,
    exportSchema = true,
)
internal abstract class TunnelchatDatabase : RoomDatabase() {
    abstract fun messages(): MessageDao
    abstract fun presence(): PresenceDao
    abstract fun stats(): StatsDao
    abstract fun logs(): LogDao

    companion object {
        fun open(context: Context, name: String = "tunnelchat.db"): TunnelchatDatabase =
            Room.databaseBuilder(context.applicationContext, TunnelchatDatabase::class.java, name)
                .fallbackToDestructiveMigration()
                .build()

        /** In-memory variant — used by tests. */
        fun inMemory(context: Context): TunnelchatDatabase =
            Room.inMemoryDatabaseBuilder(context.applicationContext, TunnelchatDatabase::class.java)
                .allowMainThreadQueries()
                .build()
    }
}
