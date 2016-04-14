#include <ccnx/forwarder/athena/athena_PIT.h>

#include <errno.h>

#include <parc/algol/parc_SafeMemory.h>
#include <parc/security/parc_CryptoHasher.h>
#include <ccnx/common/ccnx_WireFormatMessage.h>
#include <ccnx/common/codec/ccnxCodec_TlvPacket.h>
#include <ccnx/common/validation/ccnxValidation_CRC32C.h>
#include <ccnx/common/ccnx_NameSegmentNumber.h>
#include <ccnx/common/internal/ccnx_InterestDefault.h>

#include <parc/algol/parc_SafeMemory.h>
#include <parc/algol/parc_BufferComposer.h>
#include <parc/algol/parc_LinkedList.h>
#include <parc/algol/parc_Iterator.h>

#include <parc/developer/parc_StopWatch.h>

#include <stdio.h>
#include <ctype.h>

#define NUMBER_OF_LINKS 500

PARCBufferComposer *
readLine(FILE *fp)
{
    PARCBufferComposer *composer = parcBufferComposer_Create();
    char curr = fgetc(fp);
    while ((isalnum(curr) || curr == ':' || curr == '/' || curr == '.' ||
            curr == '_' || curr == '(' || curr == ')' || curr == '[' ||
            curr == ']' || curr == '-' || curr == '%' || curr == '+' ||
            curr == '=' || curr == ';' || curr == '$' || curr == '\'') && curr != EOF) {
        parcBufferComposer_PutChar(composer, curr);
        curr = fgetc(fp);
    }
    return composer;
}

void
usage()
{
    printf("pit_test <uri file> <arrival rate> <removal rate>\n");
    printf("  - uri file: path to a file containing a set of line-separated URI strings\n");
    printf("  - arrival rate: interest arrival rate (count per second)\n");
    printf("  - removal rate: content object arrival (interest removal) rate (per second)\n");
}

int
main(int argc, char *argv[argc])
{
    if (argc != 4) {
        usage();
        exit(-1);
    }

    char *fname = argv[1];
    int arrivalRate = atoi(argv[2]);
    int removalRate = atoi(argv[3]);

    FILE *file = fopen(fname, "r");
    if (file == NULL) {
        perror("Could not open file");
        usage();
        exit(-1);
    }

    // Create the PIT
    AthenaPIT *pit = athenaPIT_Create();

    PARCBitVector *egressVector = NULL;

    size_t outstanding = 0;
    size_t removedIndex = 0;
    PARCLinkedList *interestList = parcLinkedList_Create();

    size_t num = 0;
    do {
        // if we're not full, then add
        if (outstanding < arrivalRate) {
            PARCBufferComposer *composer = readLine(file);
            PARCBuffer *bufferString = parcBufferComposer_ProduceBuffer(composer);
            if (parcBuffer_Remaining(bufferString) == 0) {
                break;
            }

            char *string = parcBuffer_ToString(bufferString);
            parcBufferComposer_Release(&composer);

            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, num % NUMBER_OF_LINKS);

            fprintf(stderr, "Read: %s\n", string);

            // Create the original name and store it for later
            CCNxName *name = ccnxName_CreateFromCString(string);
            CCNxInterest *interest = ccnxInterest_CreateSimple(name);

            // Insert into the PIT
            PARCStopwatch *timer = parcStopwatch_Create();
            parcStopwatch_Start(timer);
            uint64_t startTime = parcStopwatch_ElapsedTimeNanos(timer);

            AthenaPITResolution addResult =
                athenaPIT_AddInterest(pit, interest, ingressVector, &egressVector);
            uint64_t endTime = parcStopwatch_ElapsedTimeNanos(timer);

            // TODO: do something with the timer

            parcLinkedList_Append(interestList, interest);

            // Update state
            outstanding++;
            num++;
        } else {
            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, removedIndex % NUMBER_OF_LINKS);
            CCNxInterest *interest = parcLinkedList_GetAtIndex(interestList, removedIndex);

            athenaPIT_RemoveInterest(pit, interest, ingressVector);

            // Update state
            removedIndex++;
            outstanding--;
        }
    } while (true);
}
