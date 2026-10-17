/*
 * Gift Card Reading Application
 * Original Author: Shoddycorp's Cut-Rate Contracting
 * Comments added by: Justin Cappos (JAC) and Brendan Dolan-Gavitt (BDG)
 * Maintainer:
 * Date: 8 July 2020
 */


#include "giftcard.h"

#include <stdio.h>
#include <string.h>

// fix: get_gift_card_value() was called (in print_gift_card_info() and
// gift_card_json(), below) before it was defined, and there was no
// prototype anywhere, so the compiler had to guess its signature.
int get_gift_card_value(struct this_gift_card *thisone);

// interpreter for THX-1138 assembly
void animate(char *msg, unsigned char *program) {
    unsigned char regs[16];
    char *mptr = msg;
    unsigned char *pc = program;
    int i = 0;
    int zf = 0;
    int instructions_executed = 0;
    while (1) {
        //hang: a crafted program's relative jump (case 0x09 below) can
        //leave pc net-unchanged each time through this loop (e.g. a jump
        //of -3 right after itself), spinning forever without ever
        //tripping the one-sided "pc > program+256" check further down.
        //Cap the number of instructions executed so no program can hang.
        if (instructions_executed++ > 100000) break;
        unsigned char op, arg1, arg2;
        op = *pc;
        arg1 = *(pc+1);
        arg2 = *(pc+2);
        // fix: arg1/arg2 are raw program bytes (0-255) used directly as
        // indices into regs[16] below with no bounds check; mask them
        // into range instead of indexing out of the array.
        unsigned char reg1 = arg1 & 0x0F;
        unsigned char reg2 = arg2 & 0x0F;
        switch (*pc) {
            case 0x00:
                break;
            case 0x01:
                regs[reg1] = *mptr;
                break;
            case 0x02:
                *mptr = regs[reg1];
                break;
            //crash2: mptr could be moved by an arbitrary signed amount
            //with no bounds check, and a later write through it (case
            //0x02 above) would fault once mptr had walked off of the
            //32-byte message buffer entirely. Only move the cursor if
            //the destination stays inside the message buffer.
            case 0x03:
                if (mptr + (char)arg1 >= msg && mptr + (char)arg1 < msg + 32)
                    mptr += (char)arg1;
                break;
            case 0x04:
                regs[reg2] = arg1;
                break;
            case 0x05:
                regs[reg1] ^= regs[reg2];
                zf = !regs[reg1];
                break;
            case 0x06:
                regs[reg1] += regs[reg2];
                zf = !regs[reg1];
                break;
            case 0x07:
                puts(msg);
                break;
            case 0x08:
                goto done;
            case 0x09:
                pc += (char)arg1;
                break;
            case 0x10:
                if (zf) pc += (char)arg1;
                break;
        }
        pc+=3;
        if (pc > program+256) break;
    }
done:
    return;
}

void print_gift_card_info(struct this_gift_card *thisone) {
	struct gift_card_data *gcd_ptr;
	struct gift_card_record_data *gcrd_ptr;
	struct gift_card_amount_change *gcac_ptr;
    struct gift_card_program *gcp_ptr;

	//crash1 (continued): gift_card_reader() bails out early on a bad
	//file and leaves gift_card_data NULL; without this check we'd
	//dereference that NULL pointer here instead.
	if (thisone == NULL || thisone->gift_card_data == NULL) {
		printf("  (unreadable or invalid gift card file)\n\n");
		return;
	}

	gcd_ptr = thisone->gift_card_data;
	printf("   Merchant ID: %32.32s\n",gcd_ptr->merchant_id);
	printf("   Customer ID: %32.32s\n",gcd_ptr->customer_id);
	printf("   Num records: %d\n",gcd_ptr->number_of_gift_card_records);
	for(int i=0;i<gcd_ptr->number_of_gift_card_records; i++) {
  		gcrd_ptr = (struct gift_card_record_data *) gcd_ptr->gift_card_record_data[i];
		if (gcrd_ptr->type_of_record == 1) {
			printf("      record_type: amount_change\n");
			gcac_ptr = gcrd_ptr->actual_record;
			printf("      amount_added: %d\n",gcac_ptr->amount_added);
			if (gcac_ptr->amount_added>0) {
				printf("      signature: %32.32s\n",gcac_ptr->actual_signature);
			}
		}	
		else if (gcrd_ptr->type_of_record == 2) {
			printf("      record_type: message\n");
			printf("      message: %s\n",(char *)gcrd_ptr->actual_record);
		}
		else if (gcrd_ptr->type_of_record == 3) {
            gcp_ptr = gcrd_ptr->actual_record;
			printf("      record_type: animated message\n");
            printf("      message: %s\n", gcp_ptr->message);
            printf("  [running embedded program]  \n");
            animate(gcp_ptr->message, gcp_ptr->program);
		}
	}
	printf("  Total value: %d\n\n",get_gift_card_value(thisone));
}

// Added to support web functionalities
void gift_card_json(struct this_gift_card *thisone) {
    struct gift_card_data *gcd_ptr;
    struct gift_card_record_data *gcrd_ptr;
    struct gift_card_amount_change *gcac_ptr;
    //crash1 (continued): same NULL guard as print_gift_card_info() above.
    if (thisone == NULL || thisone->gift_card_data == NULL) {
        printf("{\n  \"error\": \"unreadable or invalid gift card file\"\n}\n");
        return;
    }
    gcd_ptr = thisone->gift_card_data;
    printf("{\n");
    printf("  \"merchant_id\": \"%32.32s\",\n", gcd_ptr->merchant_id);
    printf("  \"customer_id\": \"%32.32s\",\n", gcd_ptr->customer_id);
    printf("  \"total_value\": %d,\n", get_gift_card_value(thisone));
    printf("  \"records\": [\n");
	for(int i=0;i<gcd_ptr->number_of_gift_card_records; i++) {
        gcrd_ptr = (struct gift_card_record_data *) gcd_ptr->gift_card_record_data[i];
        printf("    {\n");
        if (gcrd_ptr->type_of_record == 1) {
            printf("      \"record_type\": \"amount_change\",\n");
            gcac_ptr = gcrd_ptr->actual_record;
            printf("      \"amount_added\": %d,\n",gcac_ptr->amount_added);
            if (gcac_ptr->amount_added>0) {
                printf("      \"signature\": \"%32.32s\"\n",gcac_ptr->actual_signature);
            }
        }
        else if (gcrd_ptr->type_of_record == 2) {
			printf("      \"record_type\": \"message\",\n");
			printf("      \"message\": \"%s\"\n",(char *)gcrd_ptr->actual_record);
        }
        else if (gcrd_ptr->type_of_record == 3) {
            struct gift_card_program *gcp = gcrd_ptr->actual_record;
			printf("      \"record_type\": \"animated message\",\n");
			printf("      \"message\": \"%s\",\n",gcp->message);
            // programs are binary so we will hex for the json
            char *hexchars = "01234567890abcdef";
            char program_hex[512+1];
            program_hex[512] = '\0';
            int i;
            for(i = 0; i < 256; i++) {
                program_hex[i*2] = hexchars[((gcp->program[i] & 0xf0) >> 4)];
                program_hex[i*2+1] = hexchars[(gcp->program[i] & 0x0f)];
            }
			printf("      \"program\": \"%s\"\n",program_hex);
        }
        if (i < gcd_ptr->number_of_gift_card_records-1)
            printf("    },\n");
        else
            printf("    }\n");
    }
    printf("  ]\n");
    printf("}\n");
}

int get_gift_card_value(struct this_gift_card *thisone) {
	struct gift_card_data *gcd_ptr;
	struct gift_card_record_data *gcrd_ptr;
	struct gift_card_amount_change *gcac_ptr;
	int ret_count = 0;

	gcd_ptr = thisone->gift_card_data;
	for(int i=0;i<gcd_ptr->number_of_gift_card_records; i++) {
  		gcrd_ptr = (struct gift_card_record_data *) gcd_ptr->gift_card_record_data[i];
		if (gcrd_ptr->type_of_record == 1) {
			gcac_ptr = gcrd_ptr->actual_record;
			ret_count += gcac_ptr->amount_added;
		}	
	}
	return ret_count;
}



/* JAC: input_fd is misleading... It's a FILE type, not a fd */
struct this_gift_card *gift_card_reader(FILE *input_fd) {

	struct this_gift_card *ret_val = malloc(sizeof(struct this_gift_card));
	// fix: leave gift_card_data in a known (NULL) state so callers can
	// tell a bad/truncated file (an early break below) apart from a
	// successfully parsed one, instead of dereferencing garbage.
	ret_val->gift_card_data = NULL;

    void *optr;
	void *ptr;

	// Loop to do the whole file
	while (!feof(input_fd)) {

		struct gift_card_data *gcd_ptr;
		/* JAC: Why aren't return types checked? */
		if (fread(&ret_val->num_bytes, 4,1, input_fd) != 1) break;

		//crash1: num_bytes came straight from the file with no
		//validation; a negative (or absurdly large) value made the
		//malloc() below fail and the next fread() then wrote through
		//the resulting NULL pointer. Reject bad sizes before using them.
		if (ret_val->num_bytes <= 0 || ret_val->num_bytes > 100*1024*1024) break;

		// Make something the size of the rest and read it in
		ptr = malloc(ret_val->num_bytes);
		if (ptr == NULL) break;
		fread(ptr, ret_val->num_bytes, 1, input_fd);

        optr = ptr-4;

		gcd_ptr = ret_val->gift_card_data = malloc(sizeof(struct gift_card_data));
		gcd_ptr->merchant_id = ptr;
		ptr += 32;	
//		printf("VD: %d\n",(int)ptr - (int) gcd_ptr->merchant_id);
		gcd_ptr->customer_id = ptr;
		ptr += 32;	
		/* JAC: Something seems off here... */
		// fix: this read only 1 byte (as a signed char, so values
		// 0x80-0xFF became negative) into a 4-byte field; read the
		// full int the writer actually put there, and sanity-check it.
		gcd_ptr->number_of_gift_card_records = *((int *)ptr);
		ptr += 4;
		if (gcd_ptr->number_of_gift_card_records < 0 ||
		    gcd_ptr->number_of_gift_card_records > 100000) break;

		gcd_ptr->gift_card_record_data = (void *)malloc(gcd_ptr->number_of_gift_card_records*sizeof(void*));

		// fix: off-by-one -- this used to be i<=..., which wrote one
		// record pointer past the end of the array just allocated above.
		// Now ptr points at the gift card recrod data
		for (int i=0; i<gcd_ptr->number_of_gift_card_records; i++){
			//printf("i: %d\n",i);
			struct gift_card_record_data *gcrd_ptr;
			gcrd_ptr = gcd_ptr->gift_card_record_data[i] = malloc(sizeof(struct gift_card_record_data));
			struct gift_card_amount_change *gcac_ptr;
			// fix: was allocated with the wrong struct's size
			gcac_ptr = gcrd_ptr->actual_record = malloc(sizeof(struct gift_card_amount_change));
            struct gift_card_program *gcp_ptr;
			gcp_ptr = malloc(sizeof(struct gift_card_program));

			gcrd_ptr->record_size_in_bytes = *((char *)ptr);
            //printf("rec at %x, %d bytes\n", ptr - optr, gcrd_ptr->record_size_in_bytes); 
			ptr += 4;	
			//printf("record_data: %d\n",gcrd_ptr->record_size_in_bytes);
			gcrd_ptr->type_of_record = *((char *)ptr);
			ptr += 4;	
            //printf("type of rec: %d\n", gcrd_ptr->type_of_record);

			// amount change
			if (gcrd_ptr->type_of_record == 1) {
				gcac_ptr->amount_added = *((int*) ptr);
				ptr += 4;	

				// don't need a sig if negative
				// fix: this compared the pointer itself to 0 (always
				// false, so the signature was always read) instead of
				// amount_added, and `break` wrongly aborted all
				// remaining records instead of skipping just this one.
				if (gcac_ptr->amount_added >= 0) {
					gcac_ptr->actual_signature = ptr;
					ptr+=32;
				}
			}
			// message
			if (gcrd_ptr->type_of_record == 2) {
				gcrd_ptr->actual_record = ptr;
				// advance by the string size + 1 for nul
                // BDG: does not seem right
				ptr=ptr+strlen((char *)gcrd_ptr->actual_record)+1;
			}
            // BDG: never seen one of these in the wild
            // text animatino (BETA)
            if (gcrd_ptr->type_of_record == 3) {
                gcp_ptr->message = malloc(32);
                gcp_ptr->program = malloc(256);
                memcpy(gcp_ptr->message, ptr, 32);
                ptr+=32;
                memcpy(gcp_ptr->program, ptr, 256);
                ptr+=256;
                gcrd_ptr->actual_record = gcp_ptr;
            }
		}
	}
	return ret_val;
}

// BDG: why not a local variable here?
struct this_gift_card *thisone;

int main(int argc, char **argv) {
    // BDG: no argument checking?
    // fix: argv[1]/argv[2] were read with no check that they existed.
    if (argc < 3) {
        fprintf(stderr, "usage: %s <1|2> <file.gft>\n", argv[0]);
        return 1;
    }
	FILE *input_fd = fopen(argv[2],"r");
	if (input_fd == NULL) {
		fprintf(stderr, "could not open file: %s\n", argv[2]);
		return 1;
	}
	thisone = gift_card_reader(input_fd);
	if (argv[1][0] == '1') print_gift_card_info(thisone);
    else if (argv[1][0] == '2') gift_card_json(thisone);

	return 0;
}
