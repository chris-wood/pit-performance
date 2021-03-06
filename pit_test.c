#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include <ccnx/forwarder/athena/athena_PIT.h>

#include <parc/logging/parc_LogReporterTextStdout.h>

#include <ccnx/forwarder/metis/core/metis_Forwarder.h>
#include <ccnx/forwarder/metis/processor/metis_PIT.h>
#include <ccnx/forwarder/metis/processor/metis_StandardPIT.h>

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

#include <parc/developer/parc_Stopwatch.h>

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

    if (curr == EOF) {
        fprintf(stderr, "Done.\n");
        exit(-1);
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

static PARCBuffer *
_encodeDictionary(const CCNxTlvDictionary *dict)
{
    PARCSigner *signer = ccnxValidationCRC32C_CreateSigner();
    CCNxCodecNetworkBufferIoVec *iovec = ccnxCodecTlvPacket_DictionaryEncode((CCNxTlvDictionary *) dict, signer);
    const struct iovec *array = ccnxCodecNetworkBufferIoVec_GetArray(iovec);
    size_t iovcnt = ccnxCodecNetworkBufferIoVec_GetCount((CCNxCodecNetworkBufferIoVec *) iovec);

    size_t totalbytes = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalbytes += array[i].iov_len;
    }
    PARCBuffer *buffer = parcBuffer_Allocate(totalbytes);
    for (int i = 0; i < iovcnt; i++) {
        parcBuffer_PutArray(buffer, array[i].iov_len, array[i].iov_base);
    }
    parcBuffer_Flip(buffer);

    ccnxCodecNetworkBufferIoVec_Release(&iovec);

    return buffer;
}

static char *
_getNextURL(FILE *file)
{
    PARCBufferComposer *composer = _readLine(file, parcBufferComposer_Create());
    PARCBuffer *bufferString = parcBufferComposer_ProduceBuffer(composer);
    if (parcBuffer_Remaining(bufferString) == 0) {
        return NULL;
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

    return string;
}

typedef struct {
    ssize_t totalTime;
    size_t nameLength;
    size_t numberOfComponents;
} _PITInsertResult;

static _PITInsertResult *
_insertPITEntryMetis(FILE *file, PARCLinkedList *interestList, MetisPIT *pit, size_t index, MetisLogger *logger)
{
    _PITInsertResult *result = malloc(sizeof(_PITInsertResult));

    // PARCBitVector *egressVector = NULL;
    // PARCBitVector *ingressVector = parcBitVector_Create();
    // parcBitVector_Set(ingressVector, index % NUMBER_OF_LINKS);

    size_t ingressConnectionId = index % NUMBER_OF_LINKS;

    char *string = _getNextURL(file);
    if (string == NULL) {
        return NULL;
    }
    //fprintf(stderr, "Read: %s\n", string);

    // Create the original name and store it for later
    CCNxName *name = ccnxName_CreateFromCString(string);
    if (name == NULL) {
        return NULL;
    }
    CCNxInterest *interest = ccnxInterest_CreateSimple(name);
    PARCBuffer *buffer = _encodeDictionary(interest);

    MetisMessage *interestMessage = metisMessage_CreateFromParcBuffer(buffer, ingressConnectionId, index, logger);

    // Start a timer
    PARCStopwatch *timer = parcStopwatch_Create();
    parcStopwatch_Start(timer);

    // Insert into the PIT
    uint64_t startTime = parcStopwatch_ElapsedTimeNanos(timer);
    // athenaPIT_AddInterest(pit, interest, ingressVector, &egressVector);
    pit->receiveInterest(pit, interestMessage);
    uint64_t endTime = parcStopwatch_ElapsedTimeNanos(timer);

    // Save the result data
    result->totalTime = (endTime - startTime);
    result->nameLength = strlen(string);
    result->numberOfComponents = ccnxName_GetSegmentCount(name);

    parcMemory_Deallocate(&string);
    parcLinkedList_Append(interestList, interest);
    parcStopwatch_Release(&timer);
    ccnxName_Release(&name);
    ccnxInterest_Release(&interest);

    return result;
}

static int
testMetis(char *fname, size_t totalNumberToProcess, int arrivalRate, int removalRate)
{
    FILE *file = fopen(fname, "r");
    if (file == NULL) {
        perror("Could not open file");
        usage();
        exit(-1);
    }

    size_t outstanding = 0;
    size_t removedIndex = 0;
    PARCLinkedList *interestList = parcLinkedList_Create();

    size_t totalNum = 0;
    ssize_t numToSend = arrivalRate - removalRate + 1; // 10 - 9 + 1 = 1 + 1 = 2
    ssize_t numSent = 0;
    ssize_t numToReceive = 1;
    ssize_t numReceived = 0;
    bool fillingWindow = true;

    PARCLogReporter *reporter = parcLogReporterTextStdout_Create();
    MetisLogger *logger = metisLogger_Create(reporter, parcClock_Wallclock());

    MetisForwarder *metis = metisForwarder_Create(logger);
    MetisPIT *pit = metisStandardPIT_Create(metis);

    // TODO: generalize or duplicate the logic down below

    // MetisMessage *interest = metisMessage_CreateFromParcBuffer(PARCBuffer *buffer, unsigned ingressConnectionId, MetisTicks receiveTime, MetisLogger *logger);
    // MetisPITVerdict (*receiveInterest)(MetisPIT *pit, MetisMessage *interestMessage);
    // MetisNumberSet * (*satisfyInterest)(MetisPIT *pit, const MetisMessage *objectMessage);

    while (totalNum < totalNumberToProcess) {

        if (totalNum % 10000 == 0) {
            fprintf(stderr, "%zu\n", totalNum);
        }

        if (fillingWindow) {
            // Insert the new PIT entry
            _PITInsertResult *result = _insertPITEntryMetis(file, interestList, pit, totalNum, logger);

            if (result != NULL) {
                // Update state
                totalNum++;
                outstanding++;

                if (totalNum == removalRate) {
                    fillingWindow = false;
                }

                // Display the results
                printf("%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
            }
        } else if (numSent != numToSend) {
            // Insert the new PIT entry
            _PITInsertResult *result = _insertPITEntryMetis(file, interestList, pit, totalNum, logger);

            if (result != NULL) {
                // Update state
                totalNum++;
                numSent++;
                outstanding++;
                if (numSent >= numToSend) {
                    numReceived = 0;
                }

                // Display the results
                if (totalNum % 10000 == 0) {
                    fprintf(stderr, "%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
                }
                printf("%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
            }
        } else {
            // PARCBitVector *ingressVector = parcBitVector_Create();
            // parcBitVector_Set(ingressVector, removedIndex % NUMBER_OF_LINKS);
            CCNxInterest *interest = parcLinkedList_GetAtIndex(interestList, removedIndex);
            PARCBuffer *interestBuffer = _encodeDictionary(interest);

            MetisMessage *interestMessage = metisMessage_CreateFromParcBuffer(interestBuffer, removedIndex % NUMBER_OF_LINKS, removedIndex, logger);

            pit->removeInterest(pit, interestMessage);

            // if (!athenaPIT_RemoveInterest(pit, interest, ingressVector)) {
            //     printf("ERROR: INTEREST NOT REMOVED\n");
            // }
            // printf("%zu\n", athenaPIT_GetNumberOfTableEntries(pit));
            // parcBitVector_Release(&ingressVector);

            // Update state
            removedIndex++;
            numReceived++;
            outstanding--;
            if (numReceived == numToReceive) {
                numSent = 0;
            }
        }
    }

    return 0;
}

static _PITInsertResult *
_insertPITEntryAthena(FILE *file, PARCLinkedList *interestList, AthenaPIT *pit, size_t index)
{
    _PITInsertResult *result = malloc(sizeof(_PITInsertResult));

    PARCBitVector *egressVector = NULL;

    PARCBitVector *ingressVector = parcBitVector_Create();
    parcBitVector_Set(ingressVector, index % NUMBER_OF_LINKS);

    char *string = _getNextURL(file);
    if (string == NULL) {
        return NULL;
    }
    //fprintf(stderr, "Read: %s\n", string);

    // Create the original name and store it for later
    CCNxName *name = ccnxName_CreateFromCString(string);
    if (name == NULL) {
        return NULL;
    }
    CCNxInterest *interest = ccnxInterest_CreateSimple(name);

    // Start a timer
    PARCStopwatch *timer = parcStopwatch_Create();
    parcStopwatch_Start(timer);

    // Insert into the PIT
    uint64_t startTime = parcStopwatch_ElapsedTimeNanos(timer);
    athenaPIT_AddInterest(pit, interest, ingressVector, &egressVector);
    uint64_t endTime = parcStopwatch_ElapsedTimeNanos(timer);

    // Save the result data
    result->totalTime = (endTime - startTime);
    result->nameLength = strlen(string);
    result->numberOfComponents = ccnxName_GetSegmentCount(name);

    parcMemory_Deallocate(&string);
    parcLinkedList_Append(interestList, interest);
    parcBitVector_Release(&ingressVector);
    parcStopwatch_Release(&timer);
    ccnxName_Release(&name);
    ccnxInterest_Release(&interest);

    return result;
}

static int
testAthena(char *fname, size_t totalNumberToProcess, int arrivalRate, int removalRate)
{
    FILE *file = fopen(fname, "r");
    if (file == NULL) {
        perror("Could not open file");
        usage();
        exit(-1);
    }

    // Create the PIT
    AthenaPIT *pit = athenaPIT_Create();

    // Initialize the state
    size_t outstanding = 0;
    size_t removedIndex = 0;
    PARCLinkedList *interestList = parcLinkedList_Create();

    size_t totalNum = 0;
    ssize_t numToSend = arrivalRate - removalRate + 1; // 10 - 9 + 1 = 1 + 1 = 2
    ssize_t numSent = 0;
    ssize_t numToReceive = 1;
    ssize_t numReceived = 0;
    bool fillingWindow = true;

    // Print the arrival/removal rate to start (for the plotter)
    printf("%d\n", arrivalRate);

    while (totalNum < totalNumberToProcess) {

        if (totalNum % 10000 == 0) {
            fprintf(stderr, "%zu\n", totalNum);
        }

        if (fillingWindow) {
            // Insert the new PIT entry
            _PITInsertResult *result = _insertPITEntryAthena(file, interestList, pit, totalNum);
            if (result != NULL) {
                // Update state
                totalNum++;
                outstanding++;

                if (totalNum == removalRate) {
                    fillingWindow = false;
                }

                // Display the results
                printf("%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
            }
        } else if (numSent != numToSend) {
            // Insert the new PIT entry
            _PITInsertResult *result = _insertPITEntryAthena(file, interestList, pit, totalNum);
            if (result != NULL) {
                // Update state
                totalNum++;
                numSent++;
                outstanding++;
                if (numSent >= numToSend) {
                    numReceived = 0;
                }

                // Display the results
                if (totalNum % 10000 == 0) {
                    fprintf(stderr, "%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
                }
                printf("%zu,%zu,%zu,%zu,%zu\n", totalNum, outstanding, result->numberOfComponents, result->nameLength, result->totalTime);
            }
        } else {
            PARCBitVector *ingressVector = parcBitVector_Create();
            parcBitVector_Set(ingressVector, removedIndex % NUMBER_OF_LINKS);
            CCNxInterest *interest = parcLinkedList_GetAtIndex(interestList, removedIndex);

            if (!athenaPIT_RemoveInterest(pit, interest, ingressVector)) {
                printf("ERROR: INTEREST NOT REMOVED\n");
            }
            // printf("%zu\n", athenaPIT_GetNumberOfTableEntries(pit));
            parcBitVector_Release(&ingressVector);

            // Update state
            removedIndex++;
            numReceived++;
            outstanding--;
            if (numReceived == numToReceive) {
                numSent = 0;
            }
        }
    }

    return 0;
}

int
main(int argc, char *argv[argc])
{
    if (argc != 6) {
        usage();
        exit(-1);
    }

    char *target = argv[1];
    char *fname = argv[2];
    size_t totalNumberToProcess = atol(argv[3]);
    int arrivalRate = atoi(argv[4]);
    int removalRate = atoi(argv[5]);

    if (strcmp(target, "metis") == 0) {
        return testMetis(fname, totalNumberToProcess, arrivalRate, removalRate);
    } else if (strcmp(target, "athena") == 0) {
        return testAthena(fname, totalNumberToProcess, arrivalRate, removalRate);
    } else {
        return -1;
    }
}
