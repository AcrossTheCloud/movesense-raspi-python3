// Jussis activity broadcaster with temperature and max filtered motion value
//
#include "movesense.h"
#include "ActivityBroadcaster.h"
#include "common/core/debug.h"
#include "common/core/dbgassert.h"
#include "comm_ble/resources.h"
#include "meas_acc/resources.h"
#include "meas_temp/resources.h"
#include "ui_ind/resources.h"
#include "system_mode/resources.h"
#include "component_led/resources.h"
#include "comm_1wire/resources.h"
#include "DebugLogger.hpp"

const int ACC_SAMPLERATE = 13;
const int DATA_UPDATE_PERIOD_SECONDS = 10;
const int DATA_UPDATE_PERIOD_SAMPLES = (ACC_SAMPLERATE) * (DATA_UPDATE_PERIOD_SECONDS);
const int DATA_UPDATE_ADV_PACKET_COUNT = 1*1600; // 1000ms in 0.625ms BLE ticks / data packets per update
// LED blinking period in adertsing mode
#define LED_BLINKING_PERIOD 5000

const float MAX_NONMOVEMENT_INTENSITY = 0.05;
float motionIntensityFiltered = 0.0f;
float maxMotion = 0.0f;

const char* const ActivityBroadcaster::LAUNCHABLE_NAME = "ActTrk";
// BLE Advertise data is specified in blocks: length in bytes "len" [int8], type id [int8], content [len-1 bytes]
// NOTE: Below we use invalid CompanyID 0xFEFE. Replace with your own if you use this in product.
// CompanyID's are reserved from Bluetooth SIG (www.bluetooth.org)
uint8_t s_customAvertiseData[] = {
    0x02, 0x1,0x6,                             // Block: Flags for BLE device
    0x03, 0x3, 0x06, 0xFE,                     // 16bit UUID
    0x14, 0xFF,								   // length byte, manuf.dataID
          0x9F,0x00,                           // uint16_t for CompanyID,
          0xFF,
          0x00,0x00,0x00,0x00,                 // uint32_t for running counter
          0x00,0x00,0x00,0x00,                 // float for value 1
          0x00,0x00,0x00,0x00,                 // float for value 2
		  0x00,0x00,0x00,0x00                  // float for value 3
    };

float temperature = 123.0f;
const size_t s_dataCounterIndex = sizeof(s_customAvertiseData) -16; // Points to 8th last byte
const size_t s_dataPayloadIndex = sizeof(s_customAvertiseData) -12; // Points to 4th last byte

void ActivityBroadcaster::readTemperatureFromSensor(size_t idx)
{
    DebugLogger::info("readTemperatureFromSensor() idx: %u", idx);
    const uint8_t readTempCmd[] = {0x33, 0x08, 3}; // Read, DATA_FIFO, 3 bytes (+ 2 crc)
    WB_RES::OWCommand readTemperatureResultCmd;
    readTemperatureResultCmd.dataOut = wb::MakeArray<uint8_t>(readTempCmd, sizeof(readTempCmd));
    readTemperatureResultCmd.readCount = 5;
    mCurrentCmd = READ_TEMP;
    asyncPut(WB_RES::LOCAL::COMM_1WIRE_PEERS_CONNHANDLE(), AsyncRequestOptions(NULL, 0, true),
             mDeviceHandles[idx], readTemperatureResultCmd);
}

void ActivityBroadcaster::readFifoCountFromSensor(size_t idx)
{
    DebugLogger::info("readFifoCountFromSensor() idx: %u", idx);
    const uint8_t readTempCmd[] = {0x33, 0x07, 1}; // Read, FIFO_COUNT, 3 bytes (+ 2 crc)
    WB_RES::OWCommand readTemperatureResultCmd;
    readTemperatureResultCmd.dataOut = wb::MakeArray<uint8_t>(readTempCmd, sizeof(readTempCmd));
    readTemperatureResultCmd.readCount = 3;
    mCurrentCmd = READ_FIFO_COUNT;
    asyncPut(WB_RES::LOCAL::COMM_1WIRE_PEERS_CONNHANDLE(), AsyncRequestOptions(NULL, 0, true),
             mDeviceHandles[idx], readTemperatureResultCmd);
}

void ActivityBroadcaster::startTemperatureConversion(size_t idx)
{
    DebugLogger::info("startTemperatureConversion() idx: %u", idx);
    const uint8_t readTempCmd[1] = {0x44};
    WB_RES::OWCommand tempConversionCmd;
    tempConversionCmd.dataOut = wb::MakeArray<uint8_t>(readTempCmd, sizeof(readTempCmd));
    tempConversionCmd.readCount = 2; // crc-16
    mCurrentCmd = CONVERT_T;
    asyncPut(WB_RES::LOCAL::COMM_1WIRE_PEERS_CONNHANDLE(), AsyncRequestOptions(NULL, 0, true),
             mDeviceHandles[idx], tempConversionCmd);
}

ActivityBroadcaster::ActivityBroadcaster()
    : ResourceClient(WBDEBUG_NAME(__FUNCTION__), WB_EXEC_CTX_APPLICATION),
      LaunchableModule(LAUNCHABLE_NAME, WB_EXEC_CTX_APPLICATION),
	  mPowerOnTimer(wb::ID_INVALID_TIMER),
	  mTimer(wb::ID_INVALID_TIMER),
	  mCurrentCmd(NO_COMMAND),
      mDeviceCount(0),
      mNextIdxToMeasure(0),
      mDeviceHandles{0}	  
{
}

ActivityBroadcaster::~ActivityBroadcaster()
{
}

bool ActivityBroadcaster::initModule()
{
    mModuleState = WB_RES::ModuleStateValues::INITIALIZED;
    return true;
}

void ActivityBroadcaster::deinitModule()
{
    mModuleState = WB_RES::ModuleStateValues::UNINITIALIZED;
}

/** @see whiteboard::ILaunchableModule::startModule */
bool ActivityBroadcaster::startModule()
{
    mModuleState = WB_RES::ModuleStateValues::STARTED;
    DEBUGLOG("=============================================================================");
    DEBUGLOG("                        Module Start-up");
    asyncSubscribe(WB_RES::LOCAL::MEAS_ACC_SAMPLERATE(), AsyncRequestOptions::Empty, (int32_t)ACC_SAMPLERATE);
	asyncSubscribe(WB_RES::LOCAL::MEAS_TEMP());
    mPublishCounter = 0;
    mSamplesCollected = 0;
    mLastMovementTimestamp = 0;

    return true;
}

/** @see whiteboard::ILaunchableModule::startModule */
void ActivityBroadcaster::stopModule()
{
	wb::ResourceClient::stopTimer(mPowerOnTimer);
    wb::ResourceClient::stopTimer(mTimer);
    mPowerOnTimer = wb::ID_INVALID_TIMER;
    mTimer = wb::ID_INVALID_TIMER;
}

void ActivityBroadcaster::onNotify(whiteboard::ResourceId resourceId, const whiteboard::Value& value, const whiteboard::ParameterList& rParameters)
{
    if (resourceId.localResourceId == WB_RES::LOCAL::MEAS_ACC_SAMPLERATE::LID) {
        const WB_RES::AccData& accValue = value.convertTo<const WB_RES::AccData&>();

        for (size_t i=0; i<accValue.arrayAcc.size(); i++) {
            processIncomingData(accValue.arrayAcc[i]);
        }
    }

	else if (resourceId.localResourceId == WB_RES::LOCAL::MEAS_TEMP::LID) {
        const WB_RES::TemperatureValue& tempValue = value.convertTo<const WB_RES::TemperatureValue&>();
        maxMotion = static_cast<float>(tempValue.measurement);
    }
}

void ActivityBroadcaster::onSubscribeResult(wb::RequestId requestId,
                                           wb::ResourceId resourceId,
                                           wb::Result resultCode,
                                           const wb::Value& rResultData)
{
    DEBUGLOG("ActivityBroadcaster::onSubscribeResult() called.");
}

void ActivityBroadcaster::processIncomingData(const whiteboard::FloatVector3D &accValue)
{
    // Remove effect of gravity (high pass filter on all acc channels).
    whiteboard::FloatVector3D filteredAcc = filterAccelerationSignal(accValue);

    // Calc motion intensity per sample
    float motionIntensity = filteredAcc.x * filteredAcc.x + filteredAcc.y * filteredAcc.y + filteredAcc.z * filteredAcc.z;

    // LP Filter motion intensity to get smooth signal and avoid aliasing.
    // Since we want data on 1 second intervals, we should have maybe -12dB at 0.5Hz
    motionIntensityFiltered = filterMotionIntensity(motionIntensity);
	
	//if (motionIntensityFiltered > maxMotion) maxMotion = motionIntensityFiltered;

    // TODO: if block done, update adv data
    if (mSamplesCollected >= DATA_UPDATE_PERIOD_SAMPLES) {
		publishResults();
        mSamplesCollected=0;
		//maxMotion = 0.0f;
    }
    else {
        if(mSamplesCollected++ == DATA_UPDATE_PERIOD_SAMPLES/2) {
			// Activate 1wire bus
			asyncSubscribe(WB_RES::LOCAL::COMM_1WIRE(), AsyncRequestOptions::Empty);

			// And wait until we can start measurement
			mPowerOnTimer = ResourceClient::startTimer(50, false);
		}
    }
}

void ActivityBroadcaster::stopRunning() {
    // Unsubscribe from acc, finish in onUnsubscribeResult
    asyncUnsubscribe(WB_RES::LOCAL::MEAS_ACC_SAMPLERATE(), AsyncRequestOptions::Empty, (int32_t)ACC_SAMPLERATE);
	asyncUnsubscribe(WB_RES::LOCAL::MEAS_TEMP());
    DEBUGLOG("stopRunning()");
}

void ActivityBroadcaster::publishResults() {

    // Update data to advertise packet and increment publish counter
    mPublishCounter++;

    uint8_t *pDataBytes = reinterpret_cast<uint8_t*>(&mPublishCounter);
    for (int i=0;i<sizeof(mPublishCounter); i++) {
        s_customAvertiseData[s_dataCounterIndex + i] = *pDataBytes++;
    }

    pDataBytes = reinterpret_cast<uint8_t*>(&motionIntensityFiltered);
    for (int i=0;i<sizeof(motionIntensityFiltered); i++) {
        s_customAvertiseData[s_dataPayloadIndex + i] = *pDataBytes++;
    }

    pDataBytes = reinterpret_cast<uint8_t*>(&temperature);
    for (int i=0;i<sizeof(temperature); i++) {
        s_customAvertiseData[s_dataPayloadIndex + 4 + i] = *pDataBytes++;
    }

	pDataBytes = reinterpret_cast<uint8_t*>(&maxMotion);
    for (int i=0;i<sizeof(maxMotion); i++) {
        s_customAvertiseData[s_dataPayloadIndex + 8 + i] = *pDataBytes++;
    }

/*
    for (int i=0;i<sizeof(s_customAvertiseData); i+=4) {
        DEBUGLOG("AdvPkg: #%u: 0x%02x,0x%02x,0x%02x,0x%02x", i, s_customAvertiseData[i],s_customAvertiseData[i+1],s_customAvertiseData[i+2],s_customAvertiseData[i+3]);
    }
*/

    // Update advertising packet. We want to send the packet 10 times over 1 second data interval to maximize the chance of listener hearing it
    WB_RES::AdvSettings advSettings;
    advSettings.interval = DATA_UPDATE_ADV_PACKET_COUNT;
    advSettings.timeout = 0; // Advertise forever
    advSettings.advPacket = whiteboard::MakeArray<uint8>(s_customAvertiseData, sizeof(s_customAvertiseData));
    // Here the scanRespPacket is left default so that the device is found with the usual name.

    asyncPut(WB_RES::LOCAL::COMM_BLE_ADV_SETTINGS(), AsyncRequestOptions::Empty, advSettings);
}


float ActivityBroadcaster::filterMotionIntensity(float signalIn)
{
    // IIR filter by https://www-users.cs.york.ac.uk/~fisher/cgi-bin/mkfscript
    // Butterworth LowPass, SR=52Hz, Corner 0.35Hz, 2nd order
#define NZEROS 2
#define NPOLES 2
#define GAIN   2.303714081e+03

    static float xv[NZEROS+1], yv[NPOLES+1];

    xv[0] = xv[1];
    xv[1] = xv[2];
    xv[2] = signalIn / GAIN;
    yv[0] = yv[1];
    yv[1] = yv[2];
    yv[2] =   (xv[0] + xv[2]) + 2 * xv[1]
        + ( -0.9419453370 * yv[0]) + (  1.9402090104 * yv[1]);

    return yv[2]*2.0f; // Make it follow the tops of peaks instead of middle of energy
}

whiteboard::FloatVector3D
ActivityBroadcaster::filterAccelerationSignal(const whiteboard::FloatVector3D &accValue)
{
    // IIR filter by https://www-users.cs.york.ac.uk/~fisher/cgi-bin/mkfscript
    // Butterworth HighPass, SR=52Hz, Corner 0.5Hz
#define NZEROS 1
#define NPOLES 1
#define GAIN   ((float)1.030216813e+00)

    static whiteboard::FloatVector3D xv[NZEROS+1], yv[NPOLES+1];

    xv[0] = xv[1];
    xv[1] = accValue / GAIN;
    yv[0] = yv[1];
    yv[1] = (xv[1] - xv[0]) + (yv[0] * 0.9413389244f);

    return yv[1];
}

/*void ActivityBroadcaster::onPutResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData)
{
    switch (resourceId.localResourceId)
    {
    case WB_RES::LOCAL::COMM_BLE_ADV_SETTINGS::LID:
        // DEBUGLOG("COMM_BLE_ADV_SETTINGS returned status: %d", resultCode);
        break;
    case WB_RES::LOCAL::UI_IND_VISUAL::LID:
        // DEBUGLOG("UI_IND_VISUAL returned status: %d", resultCode);
        break;
    }
}*/

void ActivityBroadcaster::onTimer(wb::TimerId timerId)
{
    if (timerId == mPowerOnTimer)
    {
        mPowerOnTimer = wb::ID_INVALID_TIMER;
        // Start measurement by running PEERS-scan
        asyncGet(WB_RES::LOCAL::COMM_1WIRE_PEERS(), AsyncRequestOptions::Empty);
        return;
    }

    // Read previous measurement result (if available)
    if (mNextIdxToMeasure > 0)
    {
        readFifoCountFromSensor(mNextIdxToMeasure - 1);
    }
    else
    {
        // If nothing to read, start conversion here (later in onPutResult after previous device is read)
        startTemperatureConversion(mNextIdxToMeasure);
    }
}

void ActivityBroadcaster::onGetResult(wb::RequestId requestId,
                                     wb::ResourceId resourceId,
                                     wb::Result resultCode,
                                     const wb::Value& result)
{
    if (wb::IsErrorResult(resultCode))
    {
        DEBUGLOG("onGetResult returned error! return empty measurement list");
        return;
    }

    switch (resourceId.localResourceId)
    {
    case WB_RES::LOCAL::COMM_1WIRE_PEERS::LID:
    {
        // Result of scan available.
        const WB_RES::OWPeerList& peerList = result.convertTo<WB_RES::OWPeerList&>();
        DEBUGLOG("COMM_1WIRE_PEERS. size: %u", peerList.connectedPeers.size());

        // clear device array
        for (size_t i = 0; i < MAX_THERMOMETERS; i++)
        {
            //mMeasurements[i] = {0, 0.0f};
            mDeviceHandles[i] = 0;
        }
        mDeviceCount = 0;

        // For all devices matching thermometer, start operation
        size_t idx = 0;
        for (size_t i = 0; i < peerList.connectedPeers.size(); i++)
        {
            uint64_t peer_addr = peerList.connectedPeers[i].address;
            // Check family code
            constexpr uint8_t THERMOMETER_FAMILY_CODE = 0x54;
            uint8_t familyCode = (peer_addr & 0xFF);

            DEBUGLOG("peer_addr: %08X", (uint32_t)(peer_addr & 0xffffffff));
            DEBUGLOG("handle hasValue: %u", peerList.connectedPeers[i].handle.hasValue());
            DEBUGLOG("familyCode: %u", familyCode);

            if (familyCode == THERMOMETER_FAMILY_CODE && peerList.connectedPeers[i].handle.hasValue())
            {
                int32_t handle = peerList.connectedPeers[i].handle.getValue();
                DebugLogger::info("Thermometer found. handle: %d", handle);
                // Add device to list of devices
                mDeviceHandles[idx++] = handle;
                //mMeasurements[idx++].deviceId = peer_addr;
                mDeviceCount++;
            }
        }
        // Start measurement timer if matches found
        if (idx > 0)
        {
            mNextIdxToMeasure = 0;
            mTimer = wb::ResourceClient::startTimer(1, false);
        }
        else
        {
            // DeActivate 1wire bus
            asyncUnsubscribe(WB_RES::LOCAL::COMM_1WIRE(), AsyncRequestOptions::Empty);
        }
    }
    break;
    }
}

void ActivityBroadcaster::onPutResult(wb::RequestId requestId,
                                     wb::ResourceId resourceId,
                                     wb::Result resultCode,
                                     const wb::Value& result)
{
    if (wb::IsErrorResult(resultCode))
    {
        DebugLogger::error("onPutResult failed! resource: %u, result: %u", resourceId.localResourceId, resultCode);
        return;
    }

    switch (resourceId.localResourceId)
    {
	case WB_RES::LOCAL::COMM_BLE_ADV_SETTINGS::LID:
        // DEBUGLOG("COMM_BLE_ADV_SETTINGS returned status: %d", resultCode);
        break;
    case WB_RES::LOCAL::UI_IND_VISUAL::LID:
        // DEBUGLOG("UI_IND_VISUAL returned status: %d", resultCode);
        break;
    case WB_RES::LOCAL::COMM_1WIRE_PEERS_CONNHANDLE::LID:
    {
        DebugLogger::info("Put result: COMM_1WIRE_PEERS_CONNHANDLE");
        const WB_RES::OWCommandResult& cmdResult = result.convertTo<WB_RES::OWCommandResult&>();

        for (size_t i = 0; i < cmdResult.data.size(); i++)
        {
            DEBUGLOG(" #%u: %02x", i, cmdResult.data[i]);
        }

        // If read temp command, store result and start conversion
        if (mCurrentCmd == READ_FIFO_COUNT)
        {
            DEBUGLOG("READ_FIFO_COUNT: %u", cmdResult.data[0]);
            readTemperatureFromSensor(mNextIdxToMeasure - 1);
        }
        else if (mCurrentCmd == READ_TEMP)
        {
            // T = DIGITAL OUTPUT (in decimal) * 0.005
            union
            {
                int16_t i16;
                uint8_t b[2];
            } temp;
            temp.b[0] = cmdResult.data[1];
            temp.b[1] = cmdResult.data[0];
            int32_t tempC = temp.i16;
            tempC *= 5;
            DEBUGLOG("temp: %d => %d.%03u C", temp.i16, tempC / 1000, tempC % 1000);
            float fTempC = 0.005f * temp.i16;
			if (fTempC > 0.001f) temperature = fTempC;
            //mMeasurements[mNextIdxToMeasure - 1].temperature = fTempC;
            DEBUGLOG("Next device handle: %d", mDeviceHandles[mNextIdxToMeasure]);
            if (mDeviceHandles[mNextIdxToMeasure] != 0)
            {
                // Start conversion for next device
                startTemperatureConversion(mNextIdxToMeasure);
            }
            else
            {
                /*DEBUGLOG("No more devices, return measurements to GET caller");
                WB_RES::ExtTempMeasurements tempResult;
                tempResult.measurements = wb::MakeArray<WB_RES::TempMeasurement>(mMeasurements, mDeviceCount);
                wb::Request request;
                while (!mGetRequestQueue.isEmpty())
                {
                    mGetRequestQueue.pop(request);
                    returnResult(request, wb::HTTP_CODE_OK, wb::ResourceProvider::ResponseOptions::Empty, tempResult);
                }
				*/

                // DeActivate 1wire bus
                asyncUnsubscribe(WB_RES::LOCAL::COMM_1WIRE(), AsyncRequestOptions::Empty);
            }
        }
        else
        {
            // was Convert T command
            // Wait for conversion to finish before starting next measurement (timer)
            mNextIdxToMeasure++;
            mTimer = wb::ResourceClient::startTimer(16, false);
        }
        break;
    }
    }
}
