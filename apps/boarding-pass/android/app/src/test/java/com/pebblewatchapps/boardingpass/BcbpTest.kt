package com.pebblewatchapps.boardingpass

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BcbpTest {

    @Test
    fun `names the flight and the seat`() {
        assertEquals("SK4174 12A", Bcbp.label(SYNTHETIC_BCBP))
    }

    @Test
    fun `leaves out the name and the booking reference`() {
        val label = Bcbp.label(SYNTHETIC_BCBP).orEmpty()

        assertEquals(false, label.contains("TESTER"))
        assertEquals(false, label.contains("SYNTHETIC"))
        assertEquals(false, label.contains("ZZ9XY9"))
    }

    @Test
    fun `returns null for anything that is not an M1 pass`() {
        assertNull(Bcbp.label("not a boarding pass"))
        assertNull(Bcbp.label(""))
        // Right shape, wrong format code.
        assertNull(Bcbp.label("X" + SYNTHETIC_BCBP.substring(1)))
    }

    private companion object {
        const val SYNTHETIC_BCBP =
            "M1TESTER/SYNTHETIC    EZZ9XY9 OSLCPHSK 4174 250Y012A0034 147>50B0WW5180BSK 2A117000000000000SK SK 0000000000000000 20KNSYNTHETIC   "
    }
}
