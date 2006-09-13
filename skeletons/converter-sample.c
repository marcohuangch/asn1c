/*
 * Generic converter template for a selected ASN.1 type.
 * Copyright (c) 2005, 2006 Lev Walkin <vlm@lionet.info>. All rights reserved.
 * 
 * To compile with your own ASN.1 type, please redefine the PDU as shown:
 * 
 * cc -DPDU=MyCustomType -o myDecoder.o -c converter-sample.c
 */
#ifdef	HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>	/* for atoi(3) */
#include <unistd.h>	/* for getopt(3) */
#include <string.h>	/* for strerror(3) */
#include <sysexits.h>	/* for EX_* exit codes */
#include <assert.h>	/* for assert(3) */
#include <errno.h>	/* for errno */

#include <asn_application.h>
#include <asn_internal.h>	/* for _ASN_DEFAULT_STACK_MAX */

/* Convert "Type" defined by -DPDU into "asn_DEF_Type" */
#define	ASN_DEF_PDU(t)	asn_DEF_ ## t
#define	DEF_PDU_Type(t)	ASN_DEF_PDU(t)
#define	PDU_Type	DEF_PDU_Type(PDU)

extern asn_TYPE_descriptor_t PDU_Type;	/* ASN.1 type to be decoded */
#ifdef	ASN_PDU_COLLECTION		/* Generated by asn1c: -pdu=... */
extern asn_TYPE_descriptor_t *asn_pdu_collection[];
#endif

/*
 * Open file and parse its contens.
 */
static void *data_decode_from_file(asn_TYPE_descriptor_t *asnTypeOfPDU,
	const char *fname, ssize_t suggested_bufsize);
static int write_out(const void *buffer, size_t size, void *key);

       int opt_debug;	/* -d */
static int opt_check;	/* -c */
static int opt_stack;	/* -s */

/* Input data format selector */
static enum input_format {
	INP_BER,	/* -iber: BER input */
	INP_XER,	/* -ixer: XER input */
	INP_PER		/* -iper: Unaligned PER input */
} iform;	/* -i<format> */

/* Output data format selector */
static enum output_format {
	OUT_XER,	/* -oxer: XER (XML) output */
	OUT_DER,	/* -oder: DER (BER) output */
	OUT_PER,	/* -oper: Unaligned PER output */
	OUT_TEXT,	/* -otext: semi-structured text */
	OUT_NULL	/* -onull: No pretty-printing */
} oform;	/* -o<format> */

/* Debug output function */
static inline void
DEBUG(const char *fmt, ...) {
	va_list ap;
	if(!opt_debug) return;
	fprintf(stderr, "AD: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

int
main(int ac, char **av) {
	static asn_TYPE_descriptor_t *pduType = &PDU_Type;
	ssize_t suggested_bufsize = 8192;  /* close or equal to stdio buffer */
	int number_of_iterations = 1;
	int num;
	int ch;

	/* Figure out if Unaligned PER needs to be default */
	if(pduType->uper_decoder)
		iform = INP_PER;

	/*
	 * Pocess the command-line argments.
	 */
	while((ch = getopt(ac, av, "i:o:b:cdn:p:hs:")) != -1)
	switch(ch) {
	case 'i':
		if(optarg[0] == 'b') { iform = INP_BER; break; }
		if(optarg[0] == 'x') { iform = INP_XER; break; }
		if(pduType->uper_decoder
		&& optarg[0] == 'p') { iform = INP_PER; break; }
		fprintf(stderr, "-i<format>: '%s': improper format selector\n",
			optarg);
		exit(EX_UNAVAILABLE);
	case 'o':
		if(optarg[0] == 'd') { oform = OUT_DER; break; }
		if(pduType->uper_encoder
		&& optarg[0] == 'p') { oform = OUT_PER; break; }
		if(optarg[0] == 'x') { oform = OUT_XER; break; }
		if(optarg[0] == 't') { oform = OUT_TEXT; break; }
		if(optarg[0] == 'n') { oform = OUT_NULL; break; }
		fprintf(stderr, "-o<format>: '%s': improper format selector\n",
			optarg);
		exit(EX_UNAVAILABLE);
	case 'p':
#ifdef	ASN_PDU_COLLECTION
		{
			asn_TYPE_descriptor_t **pdu = asn_pdu_collection;
			if(optarg[0] < 'A' || optarg[0] > 'Z') {
				fprintf(stderr, "Available PDU types:\n");
				for(; *pdu; pdu++) printf("%s\n", (*pdu)->name);
				exit(0);
			}
			while(*pdu && strcmp((*pdu)->name, optarg)) pdu++;
			if(*pdu) { pduType = *pdu; break; }
		}
#endif	/* ASN_PDU_COLLECTION */
		fprintf(stderr, "-p %s: Unrecognized PDU\n", optarg);
		exit(EX_UNAVAILABLE);
	case 'b':
		suggested_bufsize = atoi(optarg);
		if(suggested_bufsize < 1
			|| suggested_bufsize > 16 * 1024 * 1024) {
			fprintf(stderr,
				"-b %s: Improper buffer size (1..16M)\n",
				optarg);
			exit(EX_UNAVAILABLE);
		}
		break;
	case 'c':
		opt_check = 1;
		break;
	case 'd':
		opt_debug++;	/* Double -dd means ASN.1 debug */
		break;
	case 'n':
		number_of_iterations = atoi(optarg);
		if(number_of_iterations < 1) {
			fprintf(stderr,
				"-n %s: Improper iterations count\n", optarg);
			exit(EX_UNAVAILABLE);
		}
		break;
	case 's':
		opt_stack = atoi(optarg);
		if(opt_stack < 0) {
			fprintf(stderr,
				"-s %s: Non-negative value expected\n",
				optarg);
			exit(EX_UNAVAILABLE);
		}
		break;
	case 'h':
	default:
		fprintf(stderr, "Usage: %s [options] <data.ber> ...\n", av[0]);
		fprintf(stderr, "Where options are:\n");
		if(pduType->uper_decoder)
		fprintf(stderr,
		"  -iper        Input is in Unaligned PER (Packed Encoding Rules) (DEFAULT)\n");
		fprintf(stderr,
		"  -iber        Input is in BER (Basic Encoding Rules)%s\n",
			iform == INP_PER ? "" : " (DEFAULT)");
		fprintf(stderr,
		"  -ixer        Input is in XER (XML Encoding Rules)\n");
		if(pduType->uper_encoder)
		fprintf(stderr,
		"  -oper        Output in Unaligned PER (Packed Encoding Rules)\n");
		fprintf(stderr,
		"  -oder        Output in DER (Distinguished Encoding Rules)\n"
		"  -oxer        Output in XER (XML Encoding Rules) (DEFAULT)\n"
		"  -otext       Output in plain semi-structured text (dump)\n"
		"  -onull       Verify (decode) input, but do not output\n");
#ifdef	ASN_PDU_COLLECTION
		fprintf(stderr,
		"  -p <PDU>     Specify PDU type to decode\n"
		"  -p list      List available PDUs\n");
#endif	/* ASN_PDU_COLLECTION */
		fprintf(stderr,
		"  -b <size>    Set the i/o buffer size (default is %ld)\n"
		"  -c           Check ASN.1 constraints after decoding\n"
		"  -d           Enable debugging (-dd is even better)\n"
		"  -n <num>     Process files <num> times\n"
		"  -s <size>    Set the stack usage limit (default is %d)\n"
		, (long)suggested_bufsize, _ASN_DEFAULT_STACK_MAX);
		exit(EX_USAGE);
	}

	ac -= optind;
	av += optind;

	if(ac < 1) {
		fprintf(stderr, "%s: No input files specified. "
				"Try '-h' for more information\n",
				av[-optind]);
		exit(EX_USAGE);
	}

	setvbuf(stdout, 0, _IOLBF, 0);

	for(num = 0; num < number_of_iterations; num++) {
	  int ac_i;
	  /*
	   * Process all files in turn.
	   */
	  for(ac_i = 0; ac_i < ac; ac_i++) {
		char *fname = av[ac_i];
		void *structure;
		asn_enc_rval_t erv;

		/*
		 * Decode the encoded structure from file.
		 */
		structure = data_decode_from_file(pduType,
				fname, suggested_bufsize);
		if(!structure) {
			/* Error message is already printed */
			exit(EX_DATAERR);
		}

		/* Check ASN.1 constraints */
		if(opt_check) {
			char errbuf[128];
			size_t errlen = sizeof(errbuf);
			if(asn_check_constraints(pduType, structure,
				errbuf, &errlen)) {
				fprintf(stderr, "%s: ASN.1 constraint "
					"check failed: %s\n", fname, errbuf);
				exit(EX_DATAERR);
			}
		}

		switch(oform) {
		case OUT_NULL:
			fprintf(stderr, "%s: decoded successfully\n", fname);
			break;
		case OUT_TEXT:	/* -otext */
			asn_fprint(stdout, pduType, structure);
			break;
		case OUT_XER:	/* -oxer */
			if(xer_fprint(stdout, pduType, structure)) {
				fprintf(stderr, "%s: Cannot convert into XML\n",
					fname);
				exit(EX_UNAVAILABLE);
			}
			break;
		case OUT_DER:
			erv = der_encode(pduType, structure, write_out, stdout);
			if(erv.encoded < 0) {
				fprintf(stderr, "%s: Cannot convert into DER\n",
					fname);
				exit(EX_UNAVAILABLE);
			}
			break;
		case OUT_PER:
			erv = uper_encode(pduType, structure, write_out, stdout);
			if(erv.encoded < 0) {
				fprintf(stderr, "%s: Cannot convert into Unaligned PER\n",
					fname);
				exit(EX_UNAVAILABLE);
			}
			break;
		}

		ASN_STRUCT_FREE(*pduType, structure);
	  }
	}

	return 0;
}

static struct dynamic_buffer {
	char  *data;		/* Pointer to the data bytes */
	size_t offset;		/* Offset from the start */
	size_t length;		/* Length of meaningful contents */
	size_t allocated;	/* Allocated memory for data */
	int    nreallocs;	/* Number of data reallocations */
	off_t  bytes_shifted;	/* Number of bytes ever shifted */
} DynamicBuffer;

/*
 * Ensure that the buffer contains at least this amount of free space.
 */
static void add_bytes_to_buffer(const void *data2add, size_t bySize) {

	DEBUG("add_bytes(%ld) { o=%ld l=%ld s=%ld }",
		(long)bySize,
		(long)DynamicBuffer.offset,
		(long)DynamicBuffer.length,
		(long)DynamicBuffer.allocated);

	if(DynamicBuffer.allocated
	>= (DynamicBuffer.offset + DynamicBuffer.length + bySize)) {
		DEBUG("\tNo buffer reallocation is necessary");
	} else if(bySize <= DynamicBuffer.offset) {
		DEBUG("\tContents shifted by %ld", DynamicBuffer.offset);

		/* Shift the buffer contents */
		memmove(DynamicBuffer.data,
		        DynamicBuffer.data + DynamicBuffer.offset,
			DynamicBuffer.length);
		DynamicBuffer.bytes_shifted += DynamicBuffer.offset;
		DynamicBuffer.offset = 0;
	} else {
		size_t newsize = (DynamicBuffer.allocated << 2) + bySize;
		void *p = MALLOC(newsize);
		if(!p) {
			perror("malloc()");
			exit(EX_OSERR);
		}
		memcpy(p, DynamicBuffer.data, DynamicBuffer.length);
		FREEMEM(DynamicBuffer.data);
		DynamicBuffer.data = (char *)p;
		DynamicBuffer.offset = 0;
		DynamicBuffer.allocated = newsize;
		DynamicBuffer.nreallocs++;
		DEBUG("\tBuffer reallocated to %ld (%d time)",
			newsize, DynamicBuffer.nreallocs);
	}

	memcpy(DynamicBuffer.data + DynamicBuffer.offset + DynamicBuffer.length,
		data2add, bySize);
	DynamicBuffer.length += bySize;
}

static void *data_decode_from_file(asn_TYPE_descriptor_t *pduType, const char *fname, ssize_t suggested_bufsize) {
	static char *fbuf;
	static ssize_t fbuf_size;
	static asn_codec_ctx_t s_codec_ctx;
	asn_codec_ctx_t *opt_codec_ctx = 0;
	void *structure = 0;
	asn_dec_rval_t rval;
	size_t rd;
	FILE *fp;

	if(opt_stack) {
		s_codec_ctx.max_stack_size = opt_stack;
		opt_codec_ctx = &s_codec_ctx;
	}

	if(strcmp(fname, "-")) {
		DEBUG("Processing file %s", fname);
		fp = fopen(fname, "r");
	} else {
		DEBUG("Processing %s", "standard input");
		fname = "stdin";
		fp = stdin;
	}

	if(!fp) {
		fprintf(stderr, "%s: %s\n", fname, strerror(errno));
		return 0;
	}

	/* prepare the file buffer */
	if(fbuf_size != suggested_bufsize) {
		fbuf = (char *)REALLOC(fbuf, suggested_bufsize);
		if(!fbuf) {
			perror("realloc()");
			exit(EX_OSERR);
		}
		fbuf_size = suggested_bufsize;
	}

	DynamicBuffer.offset = 0;
	DynamicBuffer.length = 0;
	DynamicBuffer.allocated = 0;
	DynamicBuffer.bytes_shifted = 0;
	DynamicBuffer.nreallocs = 0;

	/* Pretend immediate EOF */
	rval.code = RC_WMORE;
	rval.consumed = 0;

	while((rd = fread(fbuf, 1, fbuf_size, fp)) || !feof(fp)) {
		char  *i_bptr;
		size_t i_size;

		/*
		 * Copy the data over, or use the original buffer.
		 */
		if(DynamicBuffer.allocated) {
			/* Append the new data into the intermediate buffer */
			add_bytes_to_buffer(fbuf, rd);
			i_bptr = DynamicBuffer.data + DynamicBuffer.offset;
			i_size = DynamicBuffer.length;
		} else {
			i_bptr = fbuf;
			i_size = rd;
		}

		switch(iform) {
		case INP_BER:
			rval = ber_decode(opt_codec_ctx, pduType,
				(void **)&structure, i_bptr, i_size);
			break;
		case INP_XER:
			rval = xer_decode(opt_codec_ctx, pduType,
				(void **)&structure, i_bptr, i_size);
			break;
		case INP_PER:
			rval = uper_decode(opt_codec_ctx, pduType,
				(void **)&structure, i_bptr, i_size);
			break;
		}
		DEBUG("decode(%ld) consumed %ld (%ld), code %d",
			(long)DynamicBuffer.length,
			(long)rval.consumed, (long)i_size,
			rval.code);

		if(DynamicBuffer.allocated == 0) {
			/*
			 * Flush the remainder into the intermediate buffer.
			 */
			if(rval.code != RC_FAIL && rval.consumed < rd) {
				add_bytes_to_buffer(fbuf + rval.consumed,
					     rd - rval.consumed);
				rval.consumed = 0;
			}
		}

		switch(rval.code) {
		case RC_OK:
			DEBUG("RC_OK, finishing up with %ld",
				(long)rval.consumed);
			if(fp != stdin) fclose(fp);
			return structure;
		case RC_WMORE:
			/*
			 * Adjust position inside the source buffer.
			 */
			if(DynamicBuffer.allocated) {
				DynamicBuffer.offset += rval.consumed;
				DynamicBuffer.length -= rval.consumed;
			}
			DEBUG("RC_WMORE, continuing %ld with %ld..%ld..%ld",
				(long)rval.consumed,
				(long)DynamicBuffer.offset,
				(long)DynamicBuffer.length,
				(long)DynamicBuffer.allocated);
			rval.consumed = 0;
			continue;
		case RC_FAIL:
			break;
		}
		break;
	}

	fclose(fp);

	/* Clean up partially decoded structure */
	ASN_STRUCT_FREE(*pduType, structure);

	fprintf(stderr, "%s: "
		"Decode failed past byte %ld: %s\n",
		fname, (long)(DynamicBuffer.bytes_shifted
			+ DynamicBuffer.offset + rval.consumed),
		(rval.code == RC_WMORE)
			? "Unexpected end of input"
			: "Input processing error");

	return 0;
}

/* Dump the buffer out to the specified FILE */
static int write_out(const void *buffer, size_t size, void *key) {
	FILE *fp = (FILE *)key;
	return (fwrite(buffer, 1, size, fp) == size) ? 0 : -1;
}
