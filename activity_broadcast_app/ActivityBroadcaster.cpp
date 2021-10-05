// Jussis activity broadcaster with maxIntensity
//

#include "movesense.h"

#include "ActivityBroadcaster.h"
#include "common/core/debug.h"
#include "common/core/dbgassert.h"

#include "comm_ble/resources.h"
#include "meas_acc/resources.h"
#include "meas_hr/resources.h"
#include "ui_ind/resources.h"
#include "system_mode/resources.h"
#include "component_lsm6ds3/resources.h"
#include "component_led/resources.h"

const int ACC_SAMPLERATE = 52;
const int DATA_UPDATE_PERIOD_SECONDS = 1;
const int DATA_UPDATE_PERIOD_SAMPLES = (ACC_SAMPLERATE) * (DATA_UPDATE_PERIOD_SECONDS);
const int DATA_UPDATE_ADV_PACKET_COUNT = 10;

const float MAX_NONMOVEMENT_INTENSITY = 0.05;
const int NOMOVEMENT_TIMEOUT_SECONDS = 30;
static const int BUFFER_SIZE = 10;

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
float hrAverage;
float maxIntensity;
float buffer[BUFFER_SIZE];
bool led_on = 0;
const size_t s_dataCounterIndex = sizeof(s_customAvertiseData) -16; // Points to 8th last byte
const size_t s_dataPayloadIndex = sizeof(s_customAvertiseData) -12; // Points to 4th last byte

ActivityBroadcaster::ActivityBroadcaster()
    : ResourceClient(WBDEBUG_NAME(__FUNCTION__), WB_EXEC_CTX_APPLICATION),
      LaunchableModule(LAUNCHABLE_NAME, WB_EXEC_CTX_APPLICATION),
      mAlmostGoindDown(false)
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
    asyncPut(WB_RES::LOCAL::UI_IND_VISUAL(), AsyncRequestOptions::Empty, 1);

    asyncSubscribe(WB_RES::LOCAL::MEAS_ACC_SAMPLERATE(), AsyncRequestOptions::Empty, (int32_t)ACC_SAMPLERATE);
    asyncSubscribe(WB_RES::LOCAL::MEAS_HR());
    mPublishCounter = 0;
    mSamplesCollected = 0;
    // Set this to 0 here and trigger GET to /Time. The callback stores the result
    mLastMovementTimestamp = 0;
	memset(&buffer, 0, sizeof(buffer));

    return true;
}

/** @see whiteboard::ILaunchableModule::startModule */
void ActivityBroadcaster::stopModule()
{
}

void ActivityBroadcaster::onNotify(whiteboard::ResourceId resourceId, const whiteboard::Value& value, const whiteboard::ParameterList& rParameters)
{
    if (resourceId.localResourceId == WB_RES::LOCAL::MEAS_ACC_SAMPLERATE::LID) {
        const WB_RES::AccData& accValue = value.convertTo<const WB_RES::AccData&>();

        for (size_t i=0; i<accValue.arrayAcc.size(); i++) {
            processIncomingData(accValue.arrayAcc[i]);
        }
    }
    if (resourceId.localResourceId == WB_RES::LOCAL::MEAS_HR::LID) {
        const WB_RES::HRData& hrValue = value.convertTo<const WB_RES::HRData&>();
        hrAverage = hrValue.average;
    }
}

void ActivityBroadcaster::processIncomingData(const whiteboard::FloatVector3D &accValue)
{
    // Remove effect of gravity (high pass filter on all acc channels).
    whiteboard::FloatVector3D filteredAcc = filterAccelerationSignal(accValue);

    // Calc motion intensity per sample
    float motionIntensity = filteredAcc.x * filteredAcc.x + filteredAcc.y * filteredAcc.y + filteredAcc.z * filteredAcc.z;

    // LP Filter motion intensity to get smooth signal and avoid aliasing.
    // Since we want data on 1 second intervals, we should have maybe -12dB at 0.5Hz
    float motionIntensityFiltered = filterMotionIntensity(motionIntensity);

    // See if we had movement, and update timestamp if we did
    if (motionIntensity > MAX_NONMOVEMENT_INTENSITY) {
        mLastMovementTimestamp = mPublishCounter;
        if (mAlmostGoindDown) {
            DEBUGLOG("mAlmostGoindDown && motionIntensity > MAX_NONMOVEMENT_INTENSITY");
            setAlmostGoindDownMode(false);
        }
    }

    // TODO: if block done, update adv data
    if (mSamplesCollected >= DATA_UPDATE_PERIOD_SAMPLES) {

		memmove(&buffer[0], &buffer[1], (BUFFER_SIZE - 1)*sizeof(buffer[0]));
		buffer[BUFFER_SIZE-1] = motionIntensityFiltered;

		float maxVal = 0.0f;
		for (size_t i = 0; i < BUFFER_SIZE; i++) {
			if (buffer[i] > maxVal) maxVal = buffer[i];
		}
		maxIntensity = maxVal;
		// publish current data
        publishResults(motionIntensityFiltered);
        mSamplesCollected=0;

        if (mPublishCounter > mLastMovementTimestamp + NOMOVEMENT_TIMEOUT_SECONDS) {
            stopRunning();
        }
        else if (!mAlmostGoindDown && mPublishCounter > mLastMovementTimestamp + NOMOVEMENT_TIMEOUT_SECONDS) {
            DEBUGLOG("!mAlmostGoindDown && No movement. Start blinking");
            setAlmostGoindDownMode(true);
        }
    }
    else {
        mSamplesCollected++;
    }
}

void ActivityBroadcaster::stopRunning() {
    // Unsubscribe from acc, finish in onUnsubscribeResult
    asyncUnsubscribe(WB_RES::LOCAL::MEAS_ACC_SAMPLERATE(), AsyncRequestOptions::Empty, (int32_t)ACC_SAMPLERATE);
    asyncUnsubscribe(WB_RES::LOCAL::MEAS_HR());
    DEBUGLOG("stopRunning()");
}

void ActivityBroadcaster::publishResults(float motionIntensityFiltered) {

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

    pDataBytes = reinterpret_cast<uint8_t*>(&hrAverage);
    for (int i=0;i<sizeof(hrAverage); i++) {
        s_customAvertiseData[s_dataPayloadIndex + 4 + i] = *pDataBytes++;
    }
	
	pDataBytes = reinterpret_cast<uint8_t*>(&maxIntensity);
    for (int i=0;i<sizeof(maxIntensity); i++) {
        s_customAvertiseData[s_dataPayloadIndex + 8 + i] = *pDataBytes++;
    }
/*
    for (int i=0;i<sizeof(s_customAvertiseData); i+=4) {
        DEBUGLOG("AdvPkg: #%u: 0x%02x,0x%02x,0x%02x,0x%02x", i, s_customAvertiseData[i],s_customAvertiseData[i+1],s_customAvertiseData[i+2],s_customAvertiseData[i+3]);
    }
*/
    // Update advertising packet. We want to send the packet 10 times over 1 second data interval to maximize the chance of listener hearing it
    WB_RES::AdvSettings advSettings;
    advSettings.interval = 1600 / DATA_UPDATE_ADV_PACKET_COUNT; // 1000ms in 0.625ms BLE ticks / data packets per update
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

void ActivityBroadcaster::setAlmostGoindDownMode(bool isGoindDown) {
    mAlmostGoindDown = isGoindDown;
    // Start blink if almost going down, stop if not
    //uint16_t indicationType = isGoindDown ? 1 : 0; // Continuous or No indication, see ui/ind.yaml
    //asyncPut(WB_RES::LOCAL::UI_IND_VISUAL(), AsyncRequestOptions::Empty, indicationType);
}

void ActivityBroadcaster::onPutResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData)
{
    switch (resourceId.localResourceId)
    {
    case WB_RES::LOCAL::COMM_BLE_ADV_SETTINGS::LID:
        // DEBUGLOG("COMM_BLE_ADV_SETTINGS returned status: %d", resultCode);
        break;
    case WB_RES::LOCAL::COMPONENT_LSM6DS3_WAKEUP::LID:
        // DEBUGLOG("COMPONENT_LSM6DS3_WAKEUP returned status: %d", resultCode);
        if (resultCode == whiteboard::HTTP_CODE_OK) {
            // Put to FullPowerOff mode, will wakeup with movement
            asyncPut(WB_RES::LOCAL::SYSTEM_MODE(), AsyncRequestOptions::Empty, WB_RES::SystemModeValues::FULLPOWEROFF); // WB_RES::SystemMode::FULLPOWEROFF
        }
        else {
            DEBUGLOG("SHOULD NOT HAPPEN!");
            ASSERT(false);
        }
        break;
    case WB_RES::LOCAL::UI_IND_VISUAL::LID:
        // DEBUGLOG("UI_IND_VISUAL returned status: %d", resultCode);
        break;
    }
}

void ActivityBroadcaster::onUnsubscribeResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData)
{
    if (resourceId.localResourceId == WB_RES::LOCAL::MEAS_ACC_SAMPLERATE::LID)
    {
        DEBUGLOG("MEAS_ACC_SAMPLERATE unsubscribe returned status: %d", resultCode);
        // Don't really care about success or not, just setup wakeup criteria and power off
        WB_RES::WakeUpState wakeupState;
        wakeupState.state = 1; // Any movement
        wakeupState.level = 5; // ~3% movement 0..63, quite sensitive
        DEBUGLOG("Enabling activity detection on LSM6DS3");
        asyncPut(WB_RES::LOCAL::COMPONENT_LSM6DS3_WAKEUP(), AsyncRequestOptions::Empty, wakeupState);
        // Do full power off in onPutResult
    }
}
