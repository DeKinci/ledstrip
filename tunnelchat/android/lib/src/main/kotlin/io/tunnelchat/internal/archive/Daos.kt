package io.tunnelchat.internal.archive

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query

@Dao
internal interface MessageDao {
    /** Returns -1 if a row with same PK already existed (IGNORE strategy). */
    @Insert(onConflict = OnConflictStrategy.IGNORE)
    suspend fun insert(row: MessageRow): Long

    @Query("SELECT MAX(seq) FROM messages WHERE sender_id = :senderId")
    suspend fun highSeq(senderId: Int): Int?

    @Query(
        "SELECT * FROM messages " +
            "WHERE sender_id = :senderId AND seq >= :fromSeq AND seq <= :toSeq " +
            "ORDER BY seq ASC"
    )
    suspend fun range(senderId: Int, fromSeq: Int, toSeq: Int): List<MessageRow>

    @Query("SELECT COUNT(*) FROM messages")
    suspend fun count(): Int

    @Query("DELETE FROM messages")
    suspend fun clear()
}

@Dao
internal interface PresenceDao {
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(row: PresenceRow)

    @Query("SELECT * FROM presence")
    suspend fun all(): List<PresenceRow>

    @Query("SELECT * FROM presence WHERE sender_id = :senderId LIMIT 1")
    suspend fun get(senderId: Int): PresenceRow?

    @Query("DELETE FROM presence")
    suspend fun clear()
}

@Dao
internal interface StatsDao {
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun put(row: StatsRow)

    @Query("SELECT * FROM stats WHERE id = 0 LIMIT 1")
    suspend fun get(): StatsRow?

    @Query("DELETE FROM stats")
    suspend fun clear()
}

@Dao
internal interface LogDao {
    @Insert
    suspend fun append(row: LogRow): Long

    @Query("SELECT * FROM logs ORDER BY id ASC")
    suspend fun all(): List<LogRow>

    @Query("SELECT COUNT(*) FROM logs")
    suspend fun count(): Int

    /** Trim to keep newest [keep] rows by deleting the oldest. */
    @Query("DELETE FROM logs WHERE id IN (SELECT id FROM logs ORDER BY id ASC LIMIT :drop)")
    suspend fun trimOldest(drop: Int)

    @Query("DELETE FROM logs")
    suspend fun clear()
}
