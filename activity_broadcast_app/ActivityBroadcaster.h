#pragma once

#include <whiteboard/LaunchableModule.h>
#include <whiteboard/ResourceClient.h>

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

    /**
    *	Callback for asynchronous resource unsubscribe requests
    *
    *	@param requestId ID of the request
    *	@param resourceId Successful request contains ID of the resource
    *	@param resultCode Result code of the request
    *	@param rResultData Successful result contains the request result
    */
    virtual void onUnsubscribeResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData);

    /**
    *	Callback for asynchronous resource PUT requests
    *
    *	@param requestId ID of the request
    *	@param resourceId Successful request contains ID of the resource
    *	@param resultCode Result code of the request
    *	@param rResultData Successful result contains the request result
    */
    virtual void onPutResult(whiteboard::RequestId requestId, whiteboard::ResourceId resourceId, whiteboard::Result resultCode, const whiteboard::Value& rResultData);

    /**
    *	Callback for resource notifications.
    *
    *	@param resourceId Resource id associated with the update
    *	@param rValue Current value of the resource
    *	@param rParameters Notification parameters
    */
    virtual void onNotify(whiteboard::ResourceId resourceId, const whiteboard::Value& rValue, const whiteboard::ParameterList& rParameters);

protected:
    /**
    *	Timer callback.
    *
    *	@param timerId Id of timer that triggered
    */
    //virtual void onTimer(whiteboard::TimerId timerId) OVERRIDE;

private:
    uint16_t mSamplesCollected;
    uint32_t mPublishCounter;
    uint32_t mLastMovementTimestamp; // Based on pub counter
    bool mAlmostGoindDown;
    
    void setAlmostGoindDownMode(bool isGoingDown);
    void stopRunning();
    float filterMotionIntensity(float signalIn);
    whiteboard::FloatVector3D filterAccelerationSignal(const whiteboard::FloatVector3D &accValue);
    void processIncomingData(const whiteboard::FloatVector3D &accValue);
    void publishResults(float motionIntensityFiltered);
};
