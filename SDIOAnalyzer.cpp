// The MIT License (MIT)
//
// Copyright (c) 2013 Erick Fuentes http://erickfuent.es
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "SDIOAnalyzer.h"
#include "SDIOAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

#include <iostream>

SDIOAnalyzer::SDIOAnalyzer() :	Analyzer(),
	mSettings( new SDIOAnalyzerSettings() ),
	mSimulationInitilized( false ),
	mAlreadyRun(false),
	packetState(WAITING_FOR_CMD)
{
	SetAnalyzerSettings(mSettings.get());
}

SDIOAnalyzer::~SDIOAnalyzer()
{
	KillThread();
}

void SDIOAnalyzer::WorkerThread()
{
	mResults.reset( new SDIOAnalyzerResults(this, mSettings.get()));

	mAlreadyRun = true;

	SetAnalyzerResults( mResults.get());

	mResults->AddChannelBubblesWillAppearOn(mSettings->mCmdChannel);

	mClock = GetAnalyzerChannelData(mSettings->mClockChannel);
	mCmd   = GetAnalyzerChannelData(mSettings->mCmdChannel);
	mDAT0  = GetAnalyzerChannelData(mSettings->mDAT0Channel);
	mDAT1  = mSettings->mDAT1Channel == UNDEFINED_CHANNEL ? nullptr : GetAnalyzerChannelData(mSettings->mDAT1Channel);
	mDAT2  = mSettings->mDAT2Channel == UNDEFINED_CHANNEL ? nullptr : GetAnalyzerChannelData(mSettings->mDAT2Channel);
	mDAT3  = mSettings->mDAT3Channel == UNDEFINED_CHANNEL ? nullptr : GetAnalyzerChannelData(mSettings->mDAT3Channel);

	mClock->AdvanceToNextEdge();
	mCmd->AdvanceToAbsPosition(mClock->GetSampleNumber());
	mDAT0->AdvanceToAbsPosition(mClock->GetSampleNumber());
	if (mDAT1) mDAT1->AdvanceToAbsPosition(mClock->GetSampleNumber());
	if (mDAT2) mDAT2->AdvanceToAbsPosition(mClock->GetSampleNumber());
	if (mDAT3) mDAT3->AdvanceToAbsPosition(mClock->GetSampleNumber());

	packetState = WAITING_FOR_CMD;
	packetCount = 0;

	do
	{
		PacketStateMachine();

		mResults->CommitResults();
		ReportProgress(mClock->GetSampleNumber());
	}
    while (packetState != FINISHED);
}

void SDIOAnalyzer::SyncToSample(U64 sample)
{
	mCmd->AdvanceToAbsPosition(sample);
	mClock->AdvanceToAbsPosition(sample);
	mDAT0->AdvanceToAbsPosition(sample);
	if (mDAT1) mDAT1->AdvanceToAbsPosition(sample);
	if (mDAT2) mDAT2->AdvanceToAbsPosition(sample);
	if (mDAT3) mDAT3->AdvanceToAbsPosition(sample);
}

void SDIOAnalyzer::AddFrame(U64 fromsample, U64 tosample, frameTypes type, U32 data[4])
{
	Frame frame;

	frame.mStartingSampleInclusive = fromsample;
	frame.mEndingSampleInclusive = tosample;
	frame.mFlags = 0;
	frame.mData1 = ((U64)data[0] << 32) | data[1];
	frame.mData2 = ((U64)data[2] << 32) | data[2];
	frame.mType = type;
	mResults->AddFrame(frame);

	//std::cout << "Add Frame " << type << " at " << fromsample << "\n";
}

//Determine whether or not we are in a packet
void SDIOAnalyzer::PacketStateMachine()
{
    U64 prevsample;

    //printf("packetState %d\n", packetState);
	switch (packetState)
	{
	case WAITING_FOR_CMD:
        prevsample = mCmd->GetSampleNumber();
        /*
        if (!mCmd->DoMoreTransitionsExistInCurrentData())
        {
            printf("no more cmds at %llu\n", prevsample);
            packetState = FINISHED;
            break;
        }
         */
		mCmd->AdvanceToNextEdge();
        sample = mCmd->GetSampleNumber();
        if (sample < prevsample)
        {
            //printf("Moved backward?\n");
            packetState = FINISHED;
            break;
        }
        prevsample = mClock->GetSampleNumber();
        if (prevsample > sample)
        {
            //printf("Clock ahead of sample?\n");
            packetState = FINISHED;
            break;
        }
        // if clock is ahead of cmd, can't advance clock
		mClock->AdvanceToAbsPosition(sample);
		// advance to next falling edge of CMD
		//std::cout << "cmd is " << mCmd->GetBitState() << " at edge" << "\n";
		// go to clock edge where CMD is sampled
		// (falling edge normallt rising edge in HS mode (TODO))
		if (mClock->GetBitState() == BIT_HIGH) {
			mClock->AdvanceToNextEdge();
		}
		sample = mClock->GetSampleNumber();
		mCmd->AdvanceToAbsPosition(sample);
		// from here on in the clock is skipped twice to get to the next active edge
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		mClock->AdvanceToNextEdge(); // low-to-high here
		if (mCmd->GetBitState() == BIT_LOW)
		{
            int i;

            for (i = 0; i < 4; i++)
                uValue[i] = 0;
            valueBits = 0;
            valueIndex = 0;

            //printf("next cmd at %llu\n", sample);
			startSample = sample;
			packetState = PACKET_DIR;
			break;
		}
		// found a back edge, keep going waiting for high-to-low
		break;

	case PACKET_DIR:
		mClock->AdvanceToNextEdge(); // should be high-to-low here
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		if (mCmd->GetBitState() == BIT_LOW)
			// a device->host message
			uValue[0] = 0;
		else
			uValue[0] = 1;
        packetDirection = uValue[0];
		mClock->AdvanceToNextEdge(); // and rising here
		//printf("dirbit is %d at sample %d\n", packetDirection, sample);
        sample = mClock->GetSampleNumber();

		AddFrame(startSample, sample, uValue[0] ? FRAME_HOST : FRAME_CARD, uValue);

		// get 6 bits of command
		uValue[0] = 0;
        valueIndex = 0;
        valueBits = 0;
        startSample = sample;
		packetState = GET_UINT;
		packetNextState = CMD;
		packetCount = 6;
		break;

	case CMD:
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		//printf("cmd %d\n", uValue[0]);
		AddFrame(startSample, sample, FRAME_CMD, uValue);
		// get 32 bits of argument
		startSample = sample;
        packetCount = 32;

        if (packetDirection)
        {
            //  host->card, 32 bits of argument
            packetNextState = ARG;

            // setup expected response length now based on command
            //
            // CMD2, CMD9 and CMD10 respond with R2 of 136 bits
            // others with R1/3/4/5 which are 48 butes
            //
            respLength = 32;

            switch (uValue[0])
            {
            case 2:
            case 9:
            case 10:
                // R2 response is 136 bits, got 6 cmd 1 stop and 7 crc, leaves 122 bits of value
                respLength = 122;
                respType = FRAME_RESP_R2;
                break;
            case 8:
                respType = FRAME_RESP_R7;
                break;
            case 41:
                respType = FRAME_RESP_R3;
                break;
            case 55:
                respType = FRAME_RESP_R4;
                break;
            default:
                // R1 is a 48 bit response, got 6 cmd, 1 stop and 7 crc leaved 32 of value
                respType = FRAME_RESP_R1;
                break;
            }
        }
        else
        {
            packetNextState = RESPONSE;
            packetCount = respLength;
            if (packetCount == 0)
            {
                // got a response before a command, so skip this one
                //
                packetState = ARG; // just to force out clocks
                break;
            }
        }
        uValue[0] = 0;
        valueIndex = 0;
        valueBits = 0;
        packetState = GET_UINT;
		break;

	case ARG:
    case RESPONSE:
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		//printf("arg=%08X\n", uValue[0]);
		AddFrame(startSample, sample, (packetState == ARG) ? FRAME_ARG : respType, uValue);
		// get 32 bits of argument
		uValue[0] = 0;
        valueIndex = 0;
        valueBits = 0;
		startSample = sample;
		packetState = GET_UINT;
		packetNextState = CRC;
		packetCount = 7;
		break;

	case CRC:
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		//printf("crc=%02X\n", uValue[0]);
		AddFrame(startSample, sample, FRAME_CRC, uValue);
		// get 32 bits of argument
		uValue[0] = 0;
        valueIndex = 0;
        valueBits = 0;
        mClock->AdvanceToNextEdge();
		mClock->AdvanceToNextEdge();
		startSample = sample;
		packetState = STOP_BIT;
		break;

	case STOP_BIT:
		sample = mClock->GetSampleNumber();
        SyncToSample(sample);
		packetState = WAITING_FOR_CMD;
		break;

	case GET_UINT:
        if (packetCount == 0)
        {
            packetState = packetNextState;
            break;
        }
		mClock->AdvanceToNextEdge();
		sample = mClock->GetSampleNumber();
		SyncToSample(sample);
		uValue[valueIndex] <<= 1;
		if (mCmd->GetBitState() == BIT_HIGH)
		{
			uValue[valueIndex] |= 1;
		}
		mClock->AdvanceToNextEdge();
        valueBits++;
        if (valueBits >= 32)
        {
            // go to next 32 bit value each 32 bits
            valueBits = 0;
            valueIndex++;
            if (valueIndex > 3)
            {
                // can't happen, get outta here
                packetState = WAITING_FOR_CMD;
                packetCount = 0;
                break;
            }
        }
		packetCount--;
		//std::cout << packetState << " uv= " << uValue[0] << " " << packetCount << " left\n";
		if (packetCount == 0)
			packetState = packetNextState;
		break;

	default:
		//printf("AAAAA %d\n", packetState);
		break;
	}
}

bool SDIOAnalyzer::NeedsRerun()
{
	return !mAlreadyRun;
}

U32 SDIOAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor** simulation_channels )
{
    if (!mSimulationInitilized)
        mSimulationDataGenerator.Initialize(device_sample_rate, &*mSettings);

    return mSimulationDataGenerator.GenerateSimulationData(minimum_sample_index, device_sample_rate, simulation_channels);
}

U32 SDIOAnalyzer::GetMinimumSampleRateHz()
{
	return 25000;
}

const char* SDIOAnalyzer::GetAnalyzerName() const
{
	return "SDIO";
}

const char* GetAnalyzerName()
{
	return "SDIO";
}

Analyzer* CreateAnalyzer()
{
	return new SDIOAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
	delete analyzer;
}
