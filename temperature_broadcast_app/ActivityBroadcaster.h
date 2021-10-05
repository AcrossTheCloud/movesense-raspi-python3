#pragma once
#include <whiteboard/LaunchableModule.h>
#include <whiteboard/ResourceClient.h>
constexpr int MAX_THERMOMETERS = 8;
class ActivityBroadcaster FINAL : private whiteboard::ResourceClient,
                           public whiteboard::LaunchableModule
{

public:
    /** Name of this class. Used in StartupProvider list. */
    static const char* const LAUNCHABLE_NAME;
    ActivityBroadcaster();
    ~ActivityBroadcaster();

private:
    /** @see whiteboard::ILaunchableModule::initModule */
    virtual bool initModule() OVERRIDE;

    /** @see whiteboard::ILaunchableModule::deinitModule */
    virtual void deinitModule() OVERRIDE;

    /** @see whiteboard::ILaunchableModule::startModule */
    virtual bool startModule() OVERRIDE;

    /** @see whiteboard::ILaunchableModule::stopModule */
    virtual void stopModule() OVERRIDE;

    /** @see whiteboard::ResourceClient::onTimer */
    virtual void onTimer(whiteboard::TimerId timerId) OVERRIDE;
	
    /** @see whiteboard::ResourceClient::onGetResult */
    virtual void onGetResult(wb::RequestId requestId,
                             wb::ResourceId resourceId,
                             wb::Result resultCode, const
                             wb::Value& result) OVERRIDE;
							 
    /**
    *	Callback for asynchronous resource PUT requests
    *
    *	@param requestId ID of the request
    *	@param resourceId Successful request contains ID of the resource
    *	@param resultCode Result code of the request
    *	@param rResultData Successful result contains the request result
    */
    virtual void onPutResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData);
	
    /** @see whiteboard::ResourceClient::onSubscribeResult */
    virtual void onSubscribeResult(whiteboard::RequestId requestId,
                                   whiteboard::ResourceId resourceId,
                                   whiteboard::Result resultCode,
                                   const whiteboard::Value& rResultData) OVERRIDE;
								   
    /**
    *	Callback for resource notifications.
    *
    *	@param resourceId Resource id associated with the update
    *	@param rValue Current value of the resource
    *	@param rParameters Notification parameters
    */
    virtual void onNotify(whiteboard::ResourceId resourceId, const whiteboard::Value& rValue, const whiteboard::ParameterList& rParameters);
	
	void readFifoCountFromSensor(size_t idx);
    void readTemperatureFromSensor(size_t idx);
    void startTemperatureConversion(size_t idx);

private:

    uint16_t mSamplesCollected;
    uint32_t mPublishCounter;
    uint32_t mLastMovementTimestamp; // Based on pub counter

    void stopRunning();
    float filterMotionIntensity(float signalIn);
    whiteboard::FloatVector3D filterAccelerationSignal(const whiteboard::FloatVector3D &accValue);
    void processIncomingData(const whiteboard::FloatVector3D &accValue);
    //void publishResults(float motionIntensityFiltered);
	void publishResults();
    whiteboard::TimerId mPowerOnTimer;
    whiteboard::TimerId mTimer;

    enum
    {
        NO_COMMAND = 0,
        READ_FIFO_COUNT = 1,
        READ_TEMP,
        CONVERT_T
    } mCurrentCmd; // ongoing command: 1=read fifo count, 2 = read temperature, 3=convert T

    size_t mDeviceCount;
    size_t mNextIdxToMeasure;

    int32 mDeviceHandles[MAX_THERMOMETERS];

};