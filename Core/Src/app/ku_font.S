/*
 * ku_font.S
 *
 *  Created on: Jul 22, 2025
 *      Author: wuht
 */
    .syntax unified
.cpu cortex-m33
.thumb

.section .ku_font_data, "a"
.global _binary_ku_bin_start
.global _binary_ku_bin_end

_binary_ku_bin_start:
.incbin "../Core/Src/ku.bin"
_binary_ku_bin_end:

_binary_ku_bin_size = _binary_ku_bin_end - _binary_ku_bin_start
