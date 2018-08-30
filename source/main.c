/*
 *  MagicKit assembler
 *  ----
 *  This program was originaly a 6502 assembler written by J. H. Van Ornum,
 *  it has been modified and enhanced to support the PC Engine and NES consoles.
 *
 *  This program is freeware. You are free to distribute, use and modifiy it
 *  as you wish.
 *
 *  Enjoy!
 *  ----
 *  Original 6502 version by:
 *    J. H. Van Ornum
 *
 *  PC-Engine version by:
 *    David Michel
 *    Dave Shadoff
 *
 *  NES version by:
 *    Charles Doty
 *
 *  Modifications by:
 *    Bob Rost
 *    Alexey Avdyukhin
 *
 */
 
#define VERSION "v3.0b"
#define DESCRIPTION "a 6502 assembler with specific NES support"
#define GITHUB_URL "https://github.com/ClusterM/nesasm/"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <argp.h>
#include "defs.h"
#include "externs.h"
#include "protos.h"
#include "vars.h"
#include "inst.h"
#include "commit.h"

/* variables */
unsigned char ipl_buffer[4096];
char   *in_fname;	/* file names, input */
char  bin_fname[256];	/* binary */
char  lst_fname[256];	/* listing */
char  sym_fname[256];	/* symbols */
FILE *in_fp;	/* file pointers, input */
FILE *lst_fp;	/* listing */
char  section_name[4][8] = { "  ZP", " BSS", "CODE", "DATA" };
int   dump_seg;
int   zero_fill;
int   out_stdout;
int   header_opt;
int   sym_opt; /* export symbols for FCEUX flag */
char  sym_bank_offset_opt;  /* bank offset for FCEUX symbol files */
int   list_opt;		/* listing main flag */
int   mlist_opt;	/* macro listing main flag */
int   warnings_opt;	/* warnings main flag */
int   xlist;		/* listing file main flag */
int   list_level;	/* output level */
int   asm_opt[8];	/* assembler options */

/* Program description. */
static char program_desc[256];
const char *argp_program_version = program_desc;
const char *argp_program_bug_address = GITHUB_URL;

/* Program arguments description. */
static char argp_program_args_desc[] = "<source.asm>";

/* The options we understand. */
static struct argp_option options[] = {
	{ "segment-usage", 's', 0, 0, "Show (more) segment usage" },
	{ 0, 'S', 0, OPTION_HIDDEN, "" },
	{ "listing", 'i', 0, 0, "Force listing" },
	{ "macro-expansion", 'm', 0, 0, "Force macro expansion in listing" },
	{ "raw", 'r', 0, 0, "Prevent adding a ROM header" },
	{ "symbols", 'f', "<prefix>", OPTION_ARG_OPTIONAL, "Create FCEUX symbol files" },
	{ "symbols-offset", 'F', "<offset>", 0, "Bank offset for FCEUX symbol files" },
	{ "listing-level", 'l', "#", 0, "Listing file output level (0-3)" },
	{ "listing-file", 'L', "<file.lst>", 0, "Name of the listing file" },
	{ "warnings", 'W', 0, 0, "Show overflow warnings" },
	{ "output", 'o', "<file.nes>", 0, "Name of the output file" },
	{ 0 }
};

/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
	switch (key)
	{
		case 's':
			dump_seg++;
			if (dump_seg > 2) dump_seg = 2;
			break;
		case 'S':
			dump_seg = 2;
			break;
		case 'i':
			list_opt = 1;
			break;
		case 'm':
			mlist_opt = 1;
			break;
		case 'W':
			warnings_opt = 1;
			break;
		case 'r':
			header_opt = 0;
			break;
		case 'o':
  			strncpy(bin_fname, arg, sizeof(bin_fname));
  			break;
  		case 'L':
  			strncpy(lst_fname, arg, sizeof(lst_fname));
  			break;
		case 'f':
			sym_opt = 1;
			if (arg) strncpy(sym_fname, arg, sizeof(sym_fname));
			break;
		case 'F':
			sym_bank_offset_opt = atol(arg);
			break;
		case 'l':
			list_level = atol(arg);
			/* check range */
			if (list_level < 0 || list_level > 3)
				list_level = 2;
			break;
		case ARGP_KEY_ARG:
			if (state->arg_num >= 1)
				/* Too many arguments. */
				argp_usage(state);
			in_fname = arg;
			break;      
		case ARGP_KEY_END:
			if (state->arg_num < 1)
				/* Not enough arguments. */
				argp_usage (state);
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, argp_program_args_desc, NULL };

/* ----
 * main()
 * ----
 */

int
main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	int i, j;
	int ram_bank;
	
	sprintf(program_desc, "nesasm %s - %s\ncommit: %s @ %s",
		VERSION, DESCRIPTION, COMMIT, GITHUB_URL);

	if (argc == 1)
		fprintf(stderr, "%s\n", program_desc);

	/* init assembler options */
	machine = &nes;
	list_level = 2;
	header_opt = 1;
	list_opt = 0;
	mlist_opt = 0;
	warnings_opt = 0;
	sym_opt = 0;
	sym_bank_offset_opt = 0;
	zero_fill = 0;
	out_stdout = 0;

	bin_fname[0] = 0;
	lst_fname[0] = 0;
	sym_fname[0] = 0;

	/* parse command line */
	argp_parse(&argp, argc, argv, 0, 0, 0);

	/* search file extension */
	char basename[strlen(in_fname)+1];
	strcpy(basename, in_fname);
	if ((p = strrchr(basename, '.')) != NULL) {
		if (!strchr(p, PATH_SEPARATOR))
			*p = '\0';
		else
			p = NULL;
	}

	/* auto-add file extensions */
	if (!bin_fname[0])
	{
		strcpy(bin_fname, basename);
		strcat(bin_fname, machine->rom_ext);
	}
	char bin_basename[strlen(bin_fname)+1];
	strcpy(bin_basename, bin_fname);
	if ((p = strrchr(bin_basename, '.')) != NULL) {
		if (!strchr(p, PATH_SEPARATOR))
			*p = '\0';
		else
			p = NULL;
	}
	if (!lst_fname[0])
	{
		strcpy(lst_fname, bin_basename);
		strcat(lst_fname, ".lst");
	}
	if (!sym_fname[0])
	{
		strcpy(sym_fname, bin_fname);
	}

	/* init include path */
	init_path();

	/* init crc functions */
	crc_init();

	/* open the input file */
	if (open_input(in_fname)) {
		printf("Can not open input file '%s'!\n", in_fname);
		exit(1);
	}

	/* clear the ROM array */
	memset(rom, zero_fill ? 0 : 0xff, 8192 * 128);
	memset(map, zero_fill ? 0 : 0xff, 8192 * 128);

	/* clear symbol hash tables */
	for (i = 0; i < 256; i++) {
		hash_tbl[i]  = NULL;
		macro_tbl[i] = NULL;
		func_tbl[i]  = NULL;
		inst_tbl[i]  = NULL;
	}

	/* fill the instruction hash table */
	addinst(base_inst);
	addinst(base_pseudo);

	/* add machine specific instructions and pseudos */
	addinst(machine->inst);
	addinst(machine->pseudo_inst);

	/* predefined symbols */
	lablset("_bss_end", 0);
	lablset("_bank_base", 0);
	lablset("_nb_bank", 1);
	lablset("_call_bank", 0);

	/* init global variables */
	max_zp = 0x01;
	max_bss = 0x0201;
	max_bank = 0;
	rom_limit = 0x100000;		/* 1MB */
	bank_limit = 0x7F;
	bank_base = 0;
	errcnt = 0;

	/* assemble */
	for (pass = FIRST_PASS; pass <= LAST_PASS; pass++) {
		infile_error = -1;
		page = 7;
		bank = 0;
		loccnt = 0;
		slnum = 0;
		mcounter = 0;
		mcntmax = 0;
		xlist = list_opt;
		glablptr = NULL;
		skip_lines = 0;
		rsbase = 0;
		proc_nb = 0;

		/* reset assembler options */
		asm_opt[OPT_LIST] = list_opt;
		asm_opt[OPT_MACRO] = mlist_opt;
		asm_opt[OPT_WARNING] = warnings_opt;
		asm_opt[OPT_OPTIMIZE] = 0;

		/* reset bank arrays */
		for (i = 0; i < 4; i++) {
			for (j = 0; j < 256; j++) {
				bank_loccnt[i][j] = 0;
				bank_glabl[i][j]  = NULL;
				bank_page[i][j]   = 0;
			}
		}

		/* reset sections */
		ram_bank = machine->ram_bank;
		section  = S_CODE;

		/* .zp */
		section_bank[S_ZP]           = ram_bank;
		bank_page[S_ZP][ram_bank]    = machine->ram_page;
		bank_loccnt[S_ZP][ram_bank]  = 0x0000;

		/* .bss */
		section_bank[S_BSS]          = ram_bank;
		bank_page[S_BSS][ram_bank]   = machine->ram_page;
		bank_loccnt[S_BSS][ram_bank] = 0x0200;

		/* .code */
		section_bank[S_CODE]         = 0x00;
		bank_page[S_CODE][0x00]      = 0x07;
		bank_loccnt[S_CODE][0x00]    = 0x0000;

		/* .data */
		section_bank[S_DATA]         = 0x00;
		bank_page[S_DATA][0x00]      = 0x07;
		bank_loccnt[S_DATA][0x00]    = 0x0000;

		/* assemble */
		while (readline() != -1) {
			assemble();
			if (loccnt > 0x2000) {
				if (proc_ptr == NULL)
					fatal_error("Bank overflow, offset > $1FFF!");
				else {
					char tmp[256];

					sprintf(tmp, "Proc : '%s' is too large (code > 8KB)!", proc_ptr->name);
					fatal_error(tmp);
				}
				break;
			}
			if (stop_pass)
				break;
		}

		/* relocate procs */
		if (pass == FIRST_PASS)
			proc_reloc();

		/* abort pass on errors */
		if (errcnt) {
			printf("# %d error(s)\n", errcnt);
			break;
		}

		/* adjust bank base */
		if (pass == FIRST_PASS)
			bank_base = 0;

		/* update predefined symbols */
		if (pass == FIRST_PASS) {
			lablset("_bss_end", machine->ram_base + max_bss);
			lablset("_bank_base", bank_base);
			lablset("_nb_bank", max_bank + 1);
		}

		/* rewind input file */
		rewind(in_fp);

		/* open the listing file */
		if (pass == FIRST_PASS) {
			if (xlist && list_level) {
				if ((lst_fp = fopen(lst_fname, "w")) == NULL) {
					printf("Can not open listing file '%s'!\n", lst_fname);
					exit(1);
				}
				fprintf(lst_fp, "#[1]   %s\n", input_file[1].name);
			}
		}
	}

	/* rom */
	if (errcnt == 0) {
		/* save */
		/* open file */
		if (out_stdout) fp = stdout;
		else if ((fp = fopen(bin_fname, "wb")) == NULL) {
			printf("Can not open binary file '%s'!\n", bin_fname);
			exit(1);
		}
	
		/* write header */
		if (header_opt)
			machine->write_header(fp, max_bank + 1);
		
		/* write rom */
		fwrite(rom, 8192, (max_bank + 1), fp);
		fclose(fp);
	}

	/* close listing file */
	if (xlist && list_level)
		fclose(lst_fp);

	/* close input file */
	fclose(in_fp);

	if (errcnt)
		return(1);

	/* dump the bank table */
	if (dump_seg)
		show_seg_usage();

	if (sym_opt)
		stlist(sym_fname, sym_bank_offset_opt);

	/* ok */
	return(0);
}

/* ----
 * show_seg_usage()
 * ----
 */

void
show_seg_usage(void)
{
	int i, j;
	int addr, start, stop, nb;
	int rom_used;
	int rom_free;
	int ram_base = machine->ram_base;

	printf("segment usage:\n");
	printf("\n");

	/* zp usage */
	if (max_zp <= 1)
		printf("      ZP    -\n");
	else {
		start = ram_base;
		stop  = ram_base + (max_zp - 1);
		printf("      ZP    $%04X-$%04X  [%4i]\n", start, stop, stop - start + 1);
	}

	/* bss usage */
	if (max_bss <= 0x201)
		printf("     BSS    -\n");
	else {
		start = ram_base + 0x200;
		stop  = ram_base + (max_bss - 1);
		printf("     BSS    $%04X-$%04X  [%4i]\n", start, stop, stop - start + 1);
	}

	/* bank usage */
	rom_used = 0;
	rom_free = 0;

	if (max_bank)
		printf("\t\t\t\t    USED/FREE\n");

	/* scan banks */
	for (i = 0; i <= max_bank; i++) {
		start = 0;
		addr = 0;
		nb = 0;

		/* count used and free bytes */
		for (j = 0; j < 8192; j++)
			if (map[i][j] != 0xFF)
				nb++;

		/* display bank infos */
		if (nb)			
			printf("BANK% 4i    %20s    %4i/%4i\n",
					i, bank_name[i], nb, 8192 - nb);
		else {
			printf("BANK% 4i    %20s       0/8192\n", i, bank_name[i]);
			continue;
		}

		/* update used/free counters */
		rom_used += nb;
		rom_free += 8192 - nb;

		/* scan */
		if (dump_seg == 1)
			continue;

		for (;;) {
			/* search section start */
			for (; addr < 8192; addr++)
				if (map[i][addr] != 0xFF)
					break;

			/* check for end of bank */
			if (addr > 8191)
				break;

			/* get section type */
			section = map[i][addr] & 0x0F;
			page = (map[i][addr] & 0xE0) << 8;
			start = addr;

			/* search section end */
			for (; addr < 8192; addr++)
				if ((map[i][addr] & 0x0F) != section)
					break;

			/* display section infos */
			printf("    %s    $%04X-$%04X  [%4i]\n",
					section_name[section],	/* section name */
				    start + page,			/* starting address */
					addr  + page - 1,		/* end address */
					addr  - start);			/* size */
		}
	}

	/* total */
	rom_used = (rom_used + 1023) >> 10;
	rom_free = (rom_free) >> 10;
	printf("\t\t\t\t    ---- ----\n");
	printf("\t\t\t\t    %4iK%4iK\n", rom_used, rom_free);
}

