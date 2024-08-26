// didn't want to have this in one large file, but it would not flash when split into different headers and classes.
#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;


#define MAX_DELAY static_cast<size_t>(48000 * 2.f)

void transmitData();
void transmitData2();

DaisySeed  hw;
UartHandler uart;
Encoder encoder;
//3 of each object so we can have different configs of the same effect for each profile
//object stores the params but they lack getters so I had to create variables to track it
Chorus ch;
float p1chFreq, p1chDepth, p1chDelay, p2chFreq, p2chDepth, p2chDelay, p3chFreq, p3chDepth, p3chDelay;
Flanger fl;
float p1flFreq, p1flDepth, p1flDelay, p2flFreq, p2flDepth, p2flDelay, p3flFreq, p3flDepth, p3flDelay;
LadderFilter wahflt, hp, lp;
Tremolo trem;
float p1tremFreq, p1tremDepth, p2tremFreq, p2tremDepth, p3tremFreq, p3tremDepth;
Phaser ph;
float p1phFreq, p1phdepth, p2phFreq, p2phdepth, p3phFreq, p3phdepth;
//drive 
float p1dingain, p1doutgain, p1dtone, p2dingain, p2doutgain, p2dtone, p3dingain, p3doutgain, p3dtone;
//fuzz
float p1fingain, p1fuzz, p1ftone, p2fingain, p2fuzz, p2ftone, p3fingain, p3fuzz, p3ftone;




uint8_t start = 190;//transmission start bytes
uint8_t start2 = 191;//also identifies what data is coming over

float knob1, knob2, knob3, wah;

uint8_t profile = 0;


uint8_t effectCount = 0;//which effect we are currently on

uint8_t profile1[3] = {9, 9, 9};//9 is no effect
uint8_t p1Size = 0;
uint8_t profile2[3] = {9, 9, 9};
uint8_t p2Size = 0;
uint8_t profile3[3] = {9, 9, 9};
uint8_t p3Size = 0;

uint8_t errorFlag = 0;
uint8_t configuring = 0;


DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delMem1, delMem2, delMem3;

struct delay
{
    DelayLine<float, MAX_DELAY> *del;
    float                        currentDelay;
    float                        delayTarget;
    float                        feedback;

    float Process(float in)
    {
        //set delay times
        fonepole(currentDelay, delayTarget, .0002f);//low pass the pot data to smooth it out
        del->SetDelay(currentDelay);

        float read = del->Read();
        del->Write((feedback * read) + in);

        return read;
    }
};
delay del1, del2, del3;//3 delay profiles

float distort(float in, float inGain, float outGain,float tone){
    float x = in*(1+inGain*14);
    float term1, term2, term3;
    term1 = exp(x);
    term2 = exp(-x);
    term3 = exp(-x*1.2);
    float value = (term1-term3)/(term1+term2);
    x = value;
    float intone = abs(tone-1);
    hp.SetFreq(100+(intone*100));
    lp.SetFreq((15000*tone)+200);

    return hp.Process(lp.Process(x*(outGain*4+1)));
}
//Fuzz does not have many resources, and none that worked for me
//found the math online and applied to our input signal
float Fuzz(float in, float inGain, float fuzz, float tone){
    float x = in*(1+inGain*20);
    float a = 0.9*fuzz;
    float y = (8*inGain)*x/(abs(x)+1);//distort the signal first this helps with sustain after applying the fuzz
    float term1 = 1+exp((2*a)/(a-1));
    float term2 = 2*exp((a*(1-y))/(a-1));
    float term3 = term1-term2;
    float var = a/(a-1);
    float coth = cosh(var)/sinh(var) - 1;
    float out = term3*coth*-1/2 + in;

    float intone = abs(tone-1);
    hp.SetFreq(100+(intone*100));
    lp.SetFreq((15000*tone)+200);

    return hp.Process(lp.Process(out));
}


//process the samples
float effectsProcess(float in, uint8_t effect){
    wah = abs(1-hw.adc.GetFloat(3));
    wahflt.SetFreq(1200*wah+400);
    switch (effect)
    {
    case 0:
        if(profile == 0){
            return distort(in, p1dingain, p1doutgain, p1dtone);
        } else if(profile == 1){
            return distort(in, p2dingain, p2doutgain, p2dtone);
        } else if(profile == 2){
            return distort(in, p3dingain, p3doutgain, p3dtone);
        }
        break;
    case 1:
        if(profile == 0){
            return Fuzz(in, p1fingain, p1fuzz, p1ftone);
        } else if(profile == 1){
            return Fuzz(in, p2fingain, p2fuzz, p2ftone);
        } else if(profile == 2){
            return Fuzz(in, p3fingain, p3fuzz, p3ftone);
        }
        break;
    case 2:
        if(profile == 0){
            return in + del1.Process(in);
        } else if(profile == 1){
            return in + del2.Process(in);
        } else if(profile == 2){
            return in + del3.Process(in);
        }
        break;
    case 3:
        return ch.Process(in);
        break;
    case 4:
        return fl.Process(in);
        break;
    case 5:
        return wahflt.Process(in);
        break;
    case 6:
        return trem.Process(in);
        break;
    case 7:
        return ph.Process(in);
        break;
    default:
        break;
    }
    return in;
}

//get effect parameters for each effect for each profile to process as well as send to screen when needed 
float getEffectParams(uint8_t num){
    if(profile == 0){
        switch (effectCount)
        {
        case 0://distortion
            if(num == 0) {
                return p1dtone;
            } else if(num == 1){
                return p1doutgain;
            } else {
                return p1dingain;
            }
            break;
        case 1://fuzz
            if(num == 0) {
                return p1fingain;
            } else if(num == 1){
                return p1fuzz;
            } else {
                return p1ftone;
            }
            break;
        case 2://delay
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return del1.feedback;
            } else {
                return del1.delayTarget/(48000.0*2);
            }
            break;
        case 3://chorus
            if(num == 0) {
                return p1chDelay;
            } else if(num == 1){
                return p1chDepth;
            } else {
                return p1chFreq;
            }
            break;
        case 4://flanger
            if(num == 0) {
                return p1flDelay;
            } else if(num == 1){
                return p1flDepth;
            } else {
                return p1flFreq;
            }
            break;
        case 5://wah
            return 0;
            break;
        case 6://tremolo
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p1tremDepth;
            } else {
                return p1tremFreq;
            }
            break;
        case 7://phaser
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p1phFreq;
            } else {
                return p1phdepth;
            }
            break;
        }
    } else if(profile == 1){
        switch (effectCount)
        {
        case 0://distortion
            if(num == 0) {
                return p2dtone;
            } else if(num == 1){
                return p2doutgain;
            } else {
                return p2dingain;
            }
            break;
        case 1://fuzz
            if(num == 0) {
                return p2fingain;
            } else if(num == 1){
                return p2fuzz;
            } else {
                return p2ftone;
            }
            break;
        case 2://delay
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return del2.feedback;
            } else {
                return del2.delayTarget/(48000.0*2);
            }
            break;
        case 3://chorus
            if(num == 0) {
                return p2chDelay;
            } else if(num == 1){
                return p2chDepth;
            } else {
                return p2chFreq;
            }
            break;
        case 4://flanger
            if(num == 0) {
                return p2flDelay;
            } else if(num == 1){
                return p2flDepth;
            } else {
                return p2flFreq;
            }
            break;
        case 5://wah
            return 0;
            break;
        case 6://tremolo
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p2tremDepth;
            } else {
                return p2tremFreq;
            }
            break;
        case 7://phaser
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p2phFreq;
            } else {
                return p2phdepth;
            }
            break;
        }
    } else if (profile == 2){
        switch (effectCount)
        {
        case 0://distortion
            if(num == 0) {
                return p3dtone;
            } else if(num == 1){
                return p3doutgain;
            } else {
                return p3dingain;
            }
            break;
        case 1://fuzz
            if(num == 0) {
                return p3fingain;
            } else if(num == 1){
                return p3fuzz;
            } else {
                return p3ftone;
            }
            break;
        case 2://delay
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return del3.feedback;
            } else {
                return del3.delayTarget/(48000.0*2);
            }
            break;
        case 3://chorus
            if(num == 0) {
                return p3chDelay;
            } else if(num == 1){
                return p3chDepth;
            } else {
                return p3chFreq;
            }
            break;
        case 4://flanger
            if(num == 0) {
                return p3flDelay;
            } else if(num == 1){
                return p3flDepth;
            } else {
                return p3flFreq;
            }
            break;
        case 5://wah
            return 0;
            break;
        case 6://tremolo
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p3tremDepth;
            } else {
                return p3tremFreq;
            }
            break;
        case 7://phaser
            if(num == 0) {
                return 0;
            } else if(num == 1){
                return p3phFreq;
            } else {
                return p3phdepth;
            }
            break;
        }   
    }
    return 0;
}
//reads the knobs and sets the params for later use
void setEffectPrams(){
    if(profile == 0) {
        switch (effectCount)
        {
        case 0://distortion
            p1dtone = knob1;
            p1doutgain = knob2;
            p1dingain = knob3;
            break;
        case 1://fuzz
            p1fingain = knob1;
            p1fuzz = knob2;
            p1ftone = knob3;
            break;
        case 2://delay
            del1.feedback = knob2;
            del1.delayTarget = 48000.0*2*knob3;
            break;
        case 3://chorus
            ch.SetDelay(knob1);
            p1chDelay = knob1;
            ch.SetLfoDepth(knob2);
            p1chDepth = knob2;
            ch.SetLfoFreq(knob3);
            p1chFreq = knob3;
            break;
        case 4://flanger
            fl.SetDelay(knob1);
            p1flDelay = knob1;
            fl.SetLfoDepth(knob2);
            p1flDepth = knob2;
            fl.SetLfoFreq(knob3);
            p1flFreq = knob3;
            break;
        case 5://wah
            wahflt.SetFreq(1200*wah+400);
            break;
        case 6://tremolo
            trem.SetDepth(knob2);
            p1tremDepth = knob2;
            trem.SetFreq(knob3*200);
            p1tremFreq = knob3;
            break;
        case 7://phaser
            ph.SetFreq(knob2*10000);
            p1phFreq = knob2;
            ph.SetLfoFreq(knob2*10000);
            ph.SetLfoDepth(knob3);
            p1phdepth = knob3;

            break;
        
        default:
            break;
        }
    } else if(profile == 1){
        switch (effectCount)
        {
        case 0://distortion
            p2dtone = knob1;
            p2doutgain = knob2;
            p2dingain = knob3;
            break;
        case 1://fuzz
            p2ftone = knob1;
            p2fuzz = knob2;
            p2fingain = knob3;
            break;
        case 2://delay
            del2.delayTarget = 48000.0*2*knob2;
            del2.feedback = knob3;
            break;
        case 3://chorus
            ch.SetDelay(knob1);
            p2chDelay = knob1;
            ch.SetLfoDepth(knob2);
            p2chDepth = knob2;
            ch.SetLfoFreq(knob3);
            p2chFreq = knob3;
            break;
        case 4://flanger
            fl.SetDelay(knob1);
            p2flDelay = knob1;
            fl.SetLfoDepth(knob2);
            p2flDepth = knob2;
            fl.SetLfoFreq(knob3);
            p2flFreq = knob3;
            break;
        case 5://wah
            wahflt.SetFreq(1200*wah+400);
            break;
        case 6://tremolo
            trem.SetDepth(knob2);
            p2tremDepth = knob2;
            trem.SetFreq(knob3*200);
            p2tremFreq = knob3;
            break;
        case 7://phaser
            ph.SetFreq(knob2*10000);
            p2phFreq = knob2;
            ph.SetLfoFreq(knob2*10000);
            ph.SetLfoDepth(knob3);
            p2phdepth = knob3;
            break;
        default:
            break;
        }
    } else if(profile == 2){
        switch (effectCount)
        {
        case 0://distortion
            p3dtone = knob1;
            p3doutgain = knob2;
            p3dingain = knob3;
            break;
        case 1://fuzz
            p3ftone = knob1;
            p3fuzz = knob2;
            p3fingain = knob3;
            break;
        case 2://delay
            del3.delayTarget = 48000.0*2*knob2;
            del3.feedback = knob3;
            break;
        case 3://chorus
            ch.SetDelay(knob1);
            p3chDelay = knob1;
            ch.SetLfoDepth(knob2);
            p3chDepth = knob2;
            ch.SetLfoFreq(knob3);
            p3chFreq = knob3;
            break;
        case 4://flanger
            fl.SetDelay(knob1);
            p3flDelay = knob1;
            fl.SetLfoDepth(knob2);
            p3flDepth = knob2;
            fl.SetLfoFreq(knob3);
            p3flFreq = knob3;
            break;
        case 5://wah
            wahflt.SetFreq(1200*wah+400);
            break;
        case 6://tremolo
            trem.SetDepth(knob2);
            p3tremDepth = knob2;
            trem.SetFreq(knob3*200);
            p3tremFreq = knob3;
            break;
        case 7://phaser
            ph.SetFreq(knob2*10000);
            p3phFreq = knob2;
            ph.SetLfoFreq(knob2*10000);
            ph.SetLfoDepth(knob3);
            p3phdepth = knob3;

            break;
        
        default:
            break;
        }
    }//end if
}

//helper method to check if an effect is already on
bool contains(uint8_t prof){
    bool x = false;
    switch (prof){
    case 0:
        for(uint8_t i = 0; i < p1Size; i++){
            if(profile1[i] == effectCount){
                x = true;
            }
        }
        break;
    case 1:
        for(uint8_t i = 0; i < p2Size; i++){
            if(profile2[i] == effectCount){
                x = true;
            }
        }
        break;
    case 2:
        for(uint8_t i = 0; i < p3Size; i++){
            if(profile3[i] == effectCount){
                x = true;
            }
        }
        break;
    default:
        break;
    }
    return x;
}
//gets the pot data
void ReadPots(){
    knob1 = abs(1-hw.adc.GetFloat(0));
    knob2 = abs(1-hw.adc.GetFloat(1));
    knob3 = abs(1-hw.adc.GetFloat(2));
    wah = abs(1-hw.adc.GetFloat(3));
}
//removes an effect and shifts the effects down
void removeEffect() {
    int indexToRemove = -1;
    if(profile == 0){
        for (int i = 0; i < p1Size; i++) {
            if (profile1[i] == effectCount) {
                indexToRemove = i;
                break;
                }
            }
        // If the value was found
        if (indexToRemove != -1) {
            // Shift elements up to fill the gap
            for (int i = indexToRemove; i < p1Size; ++i) {
                if((i + 1 < 3)){
                    profile1[i] = profile1[i + 1];
                    profile1[i+1] = 9;
                } else {
                    profile1[i] = 9;
                }
                
            }
            // Decrease the size of the array
            --p1Size;
        }
    } else if(profile == 1) {
        for (int i = 0; i < p2Size; ++i) {
        if (profile2[i] == effectCount) {
            indexToRemove = i;
            break;
            }
        }
        // If the value was found
        if (indexToRemove != -1) {
            // Shift elements up to fill the gap
            for (int i = indexToRemove; i < p2Size; ++i) {
                if((i + 1 < 3)){
                    profile2[i] = profile2[i + 1];
                    profile2[i+1] = 9;
                } else {
                    profile2[i] = 9;
                }
                
            }
            // Decrease the size of the array
            --p2Size;
        }
    } else {
        for (int i = 0; i < p3Size; ++i) {
        if (profile3[i] == effectCount) {
            indexToRemove = i;
            break;
            }
        }
        // If the value was found
        if (indexToRemove != -1) {
            // Shift elements up to fill the gap
            for (int i = indexToRemove; i < p3Size; ++i) {
                if((i + 1 < 3)){
                    profile3[i] = profile3[i + 1];
                    profile3[i+1] = 9;
                } else {
                    profile3[i] = 9;
                }
                
            }
            // Decrease the size of the array
            --p3Size;
        }
    }
}
//handles encoder turns after initial press to save new params, cancel, turn effects on or off
//one function per profile to avoid more nested if statements
void encProfile1(){
    if((p1Size < 3) && !contains(profile)){
            profile1[p1Size] = effectCount;
            p1Size++;
            bool x = true;
            while(x){
                //set effect params here
                setEffectPrams();
                configuring = 1;
                ReadPots();
                transmitData();
                transmitData2();
                encoder.Debounce();
        
                int inc = encoder.Increment();
                if(inc > 0) {
                    p1Size--;
                    profile1[p1Size] = 9;
                    configuring = 0;
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    return;

                }
            }//end while
        } else if(contains(profile)){

            bool y = true;
            float a,b,c;
            a = getEffectParams(0);
            b = getEffectParams(1);
            c = getEffectParams(2);
            while(y) {
                configuring = 1;
                ReadPots();
                transmitData();
                transmitData2();
                //set new params for the effect here
                setEffectPrams();
                encoder.Debounce();
                int inc = encoder.Increment();
                if(inc > 0) {
                    configuring = 0;
                    //set prev values (cancel)
                    knob1 = a;
                    knob2 = b;
                    knob3 = c;
                    setEffectPrams();
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    //leave new values selected(save)
                    return;

                } else if(encoder.RisingEdge()){
                    configuring = 0;
                    removeEffect();
                    return;
                }
            }
        }

        if(p1Size > 3) {
            errorFlag = 1;
        } else {
            errorFlag = 2;
        }
        
}

void encProfile2(){
    if((p2Size < 3) && !contains(profile)){
            profile2[p2Size] = effectCount;
            p2Size++;
            bool x = true;
            while(x){
                //set effect params here
                setEffectPrams();
                configuring = 1;
                ReadPots();
                transmitData();
                transmitData2();
                encoder.Debounce();
        
                int inc = encoder.Increment();
                if(inc > 0) {
                    p2Size--;
                    profile2[p2Size] = 9;
                    configuring = 0;
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    return;

                }
            }//end while
        } else if(contains(profile)){
            bool y = true;
            float a,b,c;
            a = getEffectParams(0);
            b = getEffectParams(1);
            c = getEffectParams(2);
            while(y) {
                configuring = 1;
                ReadPots();
                transmitData();
                transmitData2();
                setEffectPrams();
                //set new params for the effect here
                encoder.Debounce();
                int inc = encoder.Increment();
                if(inc > 0) {
                    configuring = 0;
                    //set prev values (cancel)
                    knob1 = a;
                    knob2 = b;
                    knob3 = c;
                    setEffectPrams();
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    //leave new values selected(save)
                    setEffectPrams();
                    return;

                } else if(encoder.RisingEdge()){
                    configuring = 0;
                    removeEffect();
                    return;
                }
            }
        }
        if(p2Size > 3) {
            errorFlag = 1;
        } else {
            errorFlag = 2;
        }
}

void encProfile3(){
    if((p3Size < 3) && !contains(profile)){
            profile3[p3Size] = effectCount;
            p3Size++;
            bool x = true;
            while(x){
                //set effect params here
                configuring = 1;
                setEffectPrams();
                ReadPots();
                transmitData();
                transmitData();
                encoder.Debounce();
        
                int inc = encoder.Increment();
                if(inc > 0) {
                    p3Size--;
                    profile3[p3Size] = 9;
                    configuring = 0;
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    return;

                }
            }//end while
        } else if(contains(profile)){
            bool y = true;
            float a,b,c;
            a = getEffectParams(0);
            b = getEffectParams(1);
            c = getEffectParams(2);
            while(y) {
                configuring = 1;
                ReadPots();
                transmitData();
                transmitData2();
                //set new params for the effect here
                encoder.Debounce();
                int inc = encoder.Increment();
                if(inc > 0) {
                    configuring = 0;
                    //set prev values (cancel)
                    knob1 = a;
                    knob2 = b;
                    knob3 = c;
                    setEffectPrams();
                    return;

                } else if (inc < 0) {
                    configuring = 0;
                    //leave new values selected(save)
                    setEffectPrams();
                    return;

                } else if(encoder.RisingEdge()){
                    configuring = 0;
                    removeEffect();
                    return;
                }
            }
        }
        if(p3Size > 3) {
            errorFlag = 1;
        } else {
            errorFlag = 2;
        }
}



//high priority interupt when samples are ready to be processed. loops through effects and processes them
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {

    for(size_t i = 0; i < size; i++) {
        float val = in[0][i];

        if(profile == 0){
            for(uint8_t j = 0; j < p1Size; j++){
                val = effectsProcess(val, profile1[j]);
            }
        } else if(profile == 1){
            for(uint8_t j = 0; j < p2Size; j++){
                val = effectsProcess(val, profile2[j]);
            }
        } else if(profile == 2){
            for(uint8_t j = 0; j < p3Size; j++){
                val = effectsProcess(val, profile3[j]);
            }
        }
        
        out[0][i] = val;

    }
}


//inits the adc channels as well as the pin numbers
void InitializeADC()
{
	AdcChannelConfig adc_config[4];
    adc_config[0].InitSingle(hw.GetPin(17));//knob 3
    adc_config[1].InitSingle(hw.GetPin(16));//knob 2
    adc_config[2].InitSingle(hw.GetPin(15));//knob 1
    adc_config[3].InitSingle(hw.GetPin(18));//wah
	hw.adc.Init(adc_config, 4);
	hw.adc.Start();
}
//sends one byte over uart
void uartInt(uint8_t var) {
    uart.BlockingTransmit(&var, 1, 1000);
}
//sends knob and prfile data
void transmitData(){
    uartInt(start);//start for sync
    uartInt(effectCount);//which effect to display on screen
    uartInt((uint8_t)(knob1*100)+1);
    uartInt((uint8_t)(knob2*100)+1);//its params and current pot position
    uartInt((uint8_t)(knob3*100)+1);
    uartInt(profile);//profile number
    uartInt(configuring);//effect config... are you setting the knob values... 0 or 1
    uartInt(errorFlag);//error if tried to select more than 3 effects
    
    errorFlag = 0;
}
//sends effect and parameter data
void transmitData2(){
    uartInt(start2);
    switch (profile)
    {
    case 0:
        uartInt(profile1[0]);
        uartInt(profile1[1]);
        uartInt(profile1[2]);
        break;
    case 1:
        uartInt(profile2[0]);
        uartInt(profile2[1]);
        uartInt(profile2[2]);
        break;
    case 2:
        uartInt(profile3[0]);
        uartInt(profile3[1]);
        uartInt(profile3[2]);
        break;
    }

    if(contains(profile)){
            uartInt((uint8_t)(getEffectParams(0)*100)+1);//current param value
            uartInt((uint8_t)(getEffectParams(1)*100)+1);//current param value
            uartInt((uint8_t)(getEffectParams(2)*100)+1);//current param value
        } else {
            uartInt(1);//dummy
            uartInt(1);
            uartInt(1);
        }
        uartInt(1);//dummy
}

int main(void) {

    hw.Configure();
    hw.Init();

    System::Delay(1000);//need to make sure esp32 display starts before us for uart to work
    
    hw.SetAudioBlockSize(2);//sample buffer size
    //long story audiocallback causes a transient, since its periodic we get a tone at sample rate / buffer size
    //48k/2 = 24k which is inaudible
    float sr = hw.AudioSampleRate();
    ch.Init(sr);
    fl.Init(sr);
    trem.Init(sr);
    ph.Init(sr);
    wahflt.Init(sr);
    wahflt.SetFilterMode(LadderFilter::FilterMode::BP12);
    wahflt.SetRes(0.8);
    wahflt.SetPassbandGain(0.25);
    wahflt.SetInputDrive(2.2);

    hp.Init(sr);
    hp.SetFilterMode(LadderFilter::FilterMode::HP24);
    hp.SetRes(0.0);
    hp.SetInputDrive(1.0);
    hp.SetPassbandGain(1.0);
    hp.SetFreq(100);

    lp.Init(sr);
    lp.SetFilterMode(LadderFilter::FilterMode::LP24);
    lp.SetRes(0.1);
    lp.SetInputDrive(1.0);
    lp.SetPassbandGain(1.0);
    lp.SetFreq(15000);

    delMem1.Init();
    del1.del = &delMem1;
    del1.delayTarget = 2400;
    del1.feedback = 0.25;

    delMem2.Init();
    del2.del = &delMem2;
    del2.delayTarget = 2400;
    del2.feedback = 0.25;

    delMem3.Init();
    del3.del = &delMem2;
    del3.delayTarget = 2400;
    del3.feedback = 0.25;



    
    InitializeADC();

    //init footswitches and encoder
    Switch button1, button2, button3;
    button1.Init(hw.GetPin(0), 1000);
    button2.Init(hw.GetPin(1), 1000);
    button3.Init(hw.GetPin(2), 1000);
    
    encoder.Init(daisy::seed::D7,daisy::seed::D8, daisy::seed::D9);//a d7 b d8 button d9
    //uart config
    UartHandler::Config uart_conf;
    uart_conf.periph        = UartHandler::Config::Peripheral::USART_1;
    uart_conf.mode          = UartHandler::Config::Mode::TX;
    uart_conf.baudrate      = 31250;
    uart_conf.pin_config.tx = Pin(PORTB, 6);
    uart_conf.pin_config.rx = Pin(PORTB, 7);

    // Initialize the uart peripheral
    uart.Init(uart_conf);

    hw.StartAudio(AudioCallback);//start the callback
    while(1) {
        //while loop just polls switches and encoder inputs and sends data over uart
        button1.Debounce();
        if (button1.RisingEdge()) {
            profile = 2;
        }
        
        button2.Debounce();
        if (button2.RisingEdge()) {
            profile = 1;
        }
        
        button3.Debounce();
        if (button3.RisingEdge()) {
            profile = 0;
        }

        encoder.Debounce();
        
        int inc = encoder.Increment();
        if(inc > 0) {
            effectCount--;

        } else if (inc < 0) {
            effectCount++;

        }
        if((effectCount > 7) && (effectCount < 200)) {
            effectCount = 0;
        }else if(effectCount > 200){
            effectCount = 7;
        }
        if (encoder.RisingEdge()){
            if(profile == 0){
                encProfile1();
            } else if(profile == 1){
                encProfile2();
            } else if(profile == 2){
                encProfile3();
            }
        }
        
        transmitData();
        transmitData2();

    }
}