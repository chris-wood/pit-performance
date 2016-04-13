#include "../athena.c"

#include <errno.h>

#include <parc/algol/parc_SafeMemory.h>
#include <parc/security/parc_CryptoHasher.h>
#include <ccnx/common/ccnx_WireFormatMessage.h>
#include <ccnx/common/codec/ccnxCodec_TlvPacket.h>
#include <ccnx/common/validation/ccnxValidation_CRC32C.h>
#include <ccnx/common/ccnx_NameSegmentNumber.h>
#include <ccnx/common/internal/ccnx_InterestDefault.h>

int
main()
{
    PARCURI *connectionURI;
    Athena *athena = athena_Create(100);
    CCNxName *name = ccnxName_CreateFromCString("lci:/foo/bar/baz");
    CCNxInterest *interest = ccnxInterest_CreateSimple(name);

    uint64_t chunkNum = 0;
    CCNxNameSegment *chunkSegment = ccnxNameSegmentNumber_Create(CCNxNameLabelType_CHUNK, chunkNum);
    ccnxName_Append(name, chunkSegment);
    ccnxNameSegment_Release(&chunkSegment);

    PARCBuffer *payload = parcBuffer_WrapCString("this is a payload");
    CCNxContentObject *contentObject = ccnxContentObject_CreateWithNameAndPayload(name, payload);
    parcBuffer_Release(&payload);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t nowInMillis = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    ccnxContentObject_SetExpiryTime(contentObject, nowInMillis + 100000); // expire in 100 seconds

    connectionURI = parcURI_Parse("tcp://localhost:50100/listener/name=TCPListener");
    const char *result = athenaTransportLinkAdapter_Open(athena->athenaTransportLinkAdapter, connectionURI);
    assertTrue(result != NULL, "athenaTransportLinkAdapter_Open failed(%s)", strerror(errno));
    parcURI_Release(&connectionURI);

    connectionURI = parcURI_Parse("tcp://localhost:50100/name=TCP_0/local=false");
    result = athenaTransportLinkAdapter_Open(athena->athenaTransportLinkAdapter, connectionURI);
    assertTrue(result != NULL, "athenaTransportLinkAdapter_Open failed (%s)", strerror(errno));
    parcURI_Release(&connectionURI);

    connectionURI = parcURI_Parse("tcp://localhost:50100/name=TCP_1/local=false");
    result = athenaTransportLinkAdapter_Open(athena->athenaTransportLinkAdapter, connectionURI);
    assertTrue(result != NULL, "athenaTransportLinkAdapter_Open failed (%s)", strerror(errno));
    parcURI_Release(&connectionURI);

    int linkId = athenaTransportLinkAdapter_LinkNameToId(athena->athenaTransportLinkAdapter, "TCP_0");
    PARCBitVector *interestIngressVector = parcBitVector_Create();
    parcBitVector_Set(interestIngressVector, linkId);

    linkId = athenaTransportLinkAdapter_LinkNameToId(athena->athenaTransportLinkAdapter, "TCP_1");
    PARCBitVector *contentObjectIngressVector = parcBitVector_Create();
    parcBitVector_Set(contentObjectIngressVector, linkId);

    athena_EncodeMessage(interest);
    athena_EncodeMessage(contentObject);

    // Before FIB entry interest should not be forwarded
    athena_ProcessMessage(athena, interest, interestIngressVector);

    // Add route for interest, it should now be forwarded
    athenaFIB_AddRoute(athena->athenaFIB, name, contentObjectIngressVector);
    CCNxName *defaultName = ccnxName_CreateFromCString("lci:/");
    athenaFIB_AddRoute(athena->athenaFIB, defaultName, contentObjectIngressVector);
    ccnxName_Release(&defaultName);




    // Process exact interest match
    athena_ProcessMessage(athena, interest, interestIngressVector);




    // Create a matching content object that the store should retain and reply to the following interest with
    athena_ProcessMessage(athena, contentObject, contentObjectIngressVector);
    athena_ProcessMessage(athena, interest, interestIngressVector);

    parcBitVector_Release(&interestIngressVector);
    parcBitVector_Release(&contentObjectIngressVector);

    ccnxName_Release(&name);
    ccnxInterest_Release(&interest);
    ccnxInterest_Release(&contentObject);
    athena_Release(&athena);
}
