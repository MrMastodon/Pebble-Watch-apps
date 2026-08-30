package com.pebblewatchapps.boardingpass

/**
 * A boarding pass barcode in the only form the watch understands: the Aztec
 * module matrix, packed row by row with the most significant bit first.
 *
 * [label] is derived from the BCBP flight fields. It deliberately never carries
 * the passenger name or the booking reference - the watch has no use for them,
 * and anything sent there is one more place they can leak from.
 */
class EncodedPass(
    val modules: Int,
    val packed: ByteArray,
    val label: String,
)
