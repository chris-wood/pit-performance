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

static PARCBufferComposer *
_readLine(FILE *fp, PARCBufferComposer *composer)
{
    char curr = fgetc(fp);
    while ((isalnum(curr) || curr == ':' || curr == '/' || curr == '.' ||
            curr == '_' || curr == '(' || curr == ')' || curr == '[' ||
            curr == ']' || curr == '-' || curr == '%' || curr == '+' ||
            curr == '=' || curr == ';' || curr == '$' || curr == '\'') && curr != EOF) {

        if (curr == '%') {
            // pass
        } else if (curr == '=') {
            parcBufferComposer_PutChar(composer, '%');
            parcBufferComposer_PutChar(composer, '3');
            parcBufferComposer_PutChar(composer, 'D');
        } else {
            parcBufferComposer_PutChar(composer, curr);
        }

        curr = fgetc(fp);
    }

    return composer;
}

void
usage()
{
    printf("pit_test <uri file> <total number> <arrival rate> <removal rate>\n");
    printf("  - uri file: path to a file containing a set of line-separated URI strings\n");
    printf("  - total number: total number of URIs to process\n");
    printf("  - arrival rate: interest arrival rate (count per second)\n");
    printf("  - removal rate: content object arrival (interest removal) rate (per second)\n");
}

// TODO: move lookup code into a function
// TODO: move interest creation code to a function
// TODO: also include name segment count and overall length

int
main(int argc, char *argv[argc])
{
    if (argc != 5) {
        usage();
        exit(-1);
    }

    char *fname = argv[1];
    size_t totalNumberToProcess = atol(argv[2]);
    int arrivalRate = atoi(argv[3]);
    int removalRate = atoi(argv[4]);

    FILE *file = fopen(fname, "r");
    if (file == NULL) {
        perror("Could not open file");
        usage();
        exit(-1);
    }

    // Create the PIT
    AthenaPIT *pit = athenaPIT_Create();

    PARCBitVector *egressVector = NULL;

    // Initialize the state
    size_t outstanding = 0;
    size_t removedIndex = 0;
    PARCLinkedList *interestList = parcLinkedList_Create();

    size_t totalNum = 0;
    ssize_t numToSend = arrivalRate - removalRate + 1; // 10 - 9 + 1 = 1 + 1 = 2
    ssize_t numSent = 0;
    ssize_t numToReceive = 1;
    ssize_t numReceived = 0;
    bool sending = arrivalRate - removalRate > 0 ? true : false;
    bool fillingWindow = true;
    while (totalNum < totalNumberToProcess) {
        if (fillingWindow) {
            PARCBufferComposer *composer = _readLine(file, parcBufferComposer_Create());
            PARCBuffer *bufferString = parcBufferComposer_ProduceBuffer(composer);
            if (parcBuffer_Remaining(bufferString) == 0) {
                return 0;
                break;
            }

            char *string = parcBuffer_ToString(bufferString);
            parcBufferComposer_Release(&composer);

            if (strstr(string, "lci:/") == NULL || strstr(string, "ccnx:/") == NULL) {
                PARCBufferComposer *newComposer = parcBufferComposer_Create();

                parcBufferComposer_Format(newComposer, "ccnx:/%s", string);
                PARCBuffer *newBuffer = parcBufferComposer_ProduceBuffer(newComposer);
                parcBufferComposer_Release(&newComposer);

                parcMemory_Deallocate(&string);
                string = parcBuffer_ToString(newBuffer);
                parcBuffer_Release(&newBuffer);
            }

            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, totalNum % NUMBER_OF_LINKS);

            fprintf(stderr, "Read: %s\n", string);

            // Create the original name and store it for later
            CCNxName *name = ccnxName_CreateFromCString(string);
            if (name == NULL) {
                continue;
            }
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
            totalNum++;
            outstanding++;
            if (numSent == removalRate) {
                fillingWindow = false;
            }

            // Print the output
            ssize_t totalTime = endTime - startTime;
            printf("%zu,%zu,%zu,%zu\n", totalNum, outstanding, strlen(string), totalTime);

        } else if (numSent != numToSend) {
            PARCBufferComposer *composer = _readLine(file, parcBufferComposer_Create());
            PARCBuffer *bufferString = parcBufferComposer_ProduceBuffer(composer);
            if (parcBuffer_Remaining(bufferString) == 0) {
                return 0;
                break;
            }

            char *string = parcBuffer_ToString(bufferString);
            parcBufferComposer_Release(&composer);

            if (strstr(string, "lci:/") == NULL || strstr(string, "ccnx:/") == NULL) {
                PARCBufferComposer *newComposer = parcBufferComposer_Create();

                parcBufferComposer_Format(newComposer, "ccnx:/%s", string);
                PARCBuffer *newBuffer = parcBufferComposer_ProduceBuffer(newComposer);
                parcBufferComposer_Release(&newComposer);

                parcMemory_Deallocate(&string);
                string = parcBuffer_ToString(newBuffer);
                parcBuffer_Release(&newBuffer);
            }

            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, totalNum % NUMBER_OF_LINKS);

            fprintf(stderr, "Read: %s\n", string);

            // Create the original name and store it for later
            CCNxName *name = ccnxName_CreateFromCString(string);
            if (name == NULL) {
                continue;
            }
            CCNxInterest *interest = ccnxInterest_CreateSimple(name);

            // Insert into the PIT
            PARCStopwatch *timer = parcStopwatch_Create();
            parcStopwatch_Start(timer);
            uint64_t startTime = parcStopwatch_ElapsedTimeNanos(timer);

            AthenaPITResolution addResult =
                athenaPIT_AddInterest(pit, interest, ingressVector, &egressVector);
            uint64_t endTime = parcStopwatch_ElapsedTimeNanos(timer);

            parcLinkedList_Append(interestList, interest);

            // Update state
            totalNum++;
            numSent++;
            outstanding++;
            if (numSent >= numToSend) {
                numReceived = 0;
            }

            // Print the output
            ssize_t totalTime = endTime - startTime;
            printf("%zu,%zu,%zu,%zu\n", totalNum, outstanding, strlen(string), totalTime);
        } else {
            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, removedIndex % NUMBER_OF_LINKS);
            CCNxInterest *interest = parcLinkedList_GetAtIndex(interestList, removedIndex);

            athenaPIT_RemoveInterest(pit, interest, ingressVector);

            // Update state
            removedIndex++;
            numReceived++;
            outstanding--;
            if (numReceived == numToReceive) {
                numSent = 0;
            }
        }
    }
}
