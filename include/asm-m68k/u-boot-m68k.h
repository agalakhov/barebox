/*
 * Copyright (c) 2008 Carsten Schlote <c.schlote@konzeptpark.de>
 * See file CREDITS for list of people who contributed to this project.
 *
 * This file is part of U-Boot V2.
 *
 * U-Boot V2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * U-Boot V2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with U-Boot V2.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  Arch dependant U-Boot defines
 */
#ifndef _U_BOOT_M68K_H_
#define _U_BOOT_M68K_H_	1

/* for the following variables, see start.S */
//extern ulong _armboot_start;	/* code start */
//extern ulong _bss_start;	/* code + data end == BSS start */
//extern ulong _bss_end;		/* BSS end */
//extern ulong IRQ_STACK_START;	/* top of IRQ stack */

/* cpu/.../cpu.c */
int	cleanup_before_linux(void);

/* board/.../... */
//int	board_init(void);
//int	dram_init (void);

#endif	/* _U_BOOT_M68K_H_ */