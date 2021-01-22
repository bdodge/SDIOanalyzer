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

#include "SDIOSimulationDataGenerator.h"
#include "SDIOAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

static struct sdiocmd {
    bool iscmd;
    uint8_t cmd;
    uint32_t arg;
    uint8_t crc;
}
s_sim_cmd[] = {
    { true, 55, 0xfeedface, 0x12 },
    { false, 55, 0xdeadbeed, 0x21 },
    { true, 41, 0xb00bface, 0x55 },
    { false, 41, 0x12345678, 0xAA }
};

SDIOSimulationDataGenerator::SDIOSimulationDataGenerator() : mCmdIndex( 0 )
{
}

SDIOSimulationDataGenerator::~SDIOSimulationDataGenerator()
{
}

void SDIOSimulationDataGenerator::Initialize( U32 simulation_sample_rate, SDIOAnalyzerSettings* settings )
{
	mSimulationSampleRateHz = simulation_sample_rate;
	mSettings = settings;

    mSDIOSimulationCmd.SetChannel( mSettings->mCmdChannel );
    mSDIOSimulationCmd.SetSampleRate( simulation_sample_rate );
    mSDIOSimulationClk.SetChannel( mSettings->mClockChannel );
    mSDIOSimulationClk.SetSampleRate( simulation_sample_rate );

    mSDIOSimulationCmd.SetInitialBitState( BIT_HIGH );
    mSDIOSimulationClk.SetInitialBitState( BIT_HIGH );
}

U32 SDIOSimulationDataGenerator::GenerateSimulationData( U64 largest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel )
{
	U64 adjusted_largest_sample_requested = AnalyzerHelpers::AdjustSimulationTargetSample( largest_sample_requested, sample_rate, mSimulationSampleRateHz );
    
    samples_per_bit = mSimulationSampleRateHz / 1000;

    samples_per_bit += 1;
    samples_per_bit &= ~1;

    // scoot fwd a bit
    mSDIOSimulationCmd.Advance(samples_per_bit * 10);
    mSDIOSimulationClk.Advance(samples_per_bit * 10);

	while( mSDIOSimulationCmd.GetCurrentSampleNumber() < adjusted_largest_sample_requested )
	{
		CreateSDIO();
	}

	*simulation_channel = &mSDIOSimulationCmd;
	return 1;
}

void SDIOSimulationDataGenerator::SDIOaddUINT(uint32_t val, int bits)
{
    U32 mask;
    int i;
    
    // add 6 bits for command
    for(i = 0, mask = 1 << (bits - 1); i < bits; i++)
    {
        if((val & mask ) != 0)
            mSDIOSimulationCmd.TransitionIfNeeded( BIT_HIGH );
        else
            mSDIOSimulationCmd.TransitionIfNeeded( BIT_LOW );
        SDIOclockIt();
        mSDIOSimulationCmd.Advance(samples_per_bit);
        mask = mask >> 1;
    }
}

void SDIOSimulationDataGenerator::SDIOclockIt()
{
    mSDIOSimulationClk.Transition();
    mSDIOSimulationClk.Advance( samples_per_bit / 2 );
    mSDIOSimulationClk.Transition();
    mSDIOSimulationClk.Advance( samples_per_bit / 2 );
}

void SDIOSimulationDataGenerator::CreateSDIO()
{
    struct sdiocmd *cmd = &s_sim_cmd[mCmdIndex];
   
	mCmdIndex++;
	if( mCmdIndex == sizeof(s_sim_cmd)/sizeof(struct sdiocmd))
		mCmdIndex = 0;

	mSDIOSimulationCmd.Transition();  //low-going edge for start bit
    SDIOclockIt();
    mSDIOSimulationCmd.Advance( samples_per_bit );  //add start bit time
 
    // add direction bit
    //
    if (cmd->iscmd)
        mSDIOSimulationCmd.Transition();
    SDIOclockIt();
    mSDIOSimulationCmd.Advance( samples_per_bit );  //add start bit time

    SDIOaddUINT(cmd->cmd, 6);
    SDIOaddUINT(cmd->arg, 32);
    SDIOaddUINT(cmd->crc, 7);
    
    // stop bit
	mSDIOSimulationCmd.TransitionIfNeeded( BIT_HIGH ); //we need to end high
    SDIOclockIt();
    mSDIOSimulationCmd.Advance( samples_per_bit );
    
    /*
    // sanity - cmd and clock should end on same sample here
    //
    U64 csamp = mSDIOSimulationCmd.GetCurrentSampleNumber();
    U64 ksamp = mSDIOSimulationClk.GetCurrentSampleNumber();
    if (csamp != ksamp)
        printf("cmd=%llu clk=%llu\n", csamp, ksamp);
    */
}
