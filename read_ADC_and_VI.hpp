#pragma once

#include <ADS1115_lite.h>

#define ADC_COUNTS 32768
#define CT_BURDEN_RESISTOR 62
#define CT_TURNS 1800

ADS1115_lite adc(ADS1115_DEFAULT_ADDRESS); 

double VCAL = 124;
double ICAL = 0.55;
double PHASECAL = 1.7;

int startV;
int sampleV, sampleI, lastsampleV, lastsampleI;
double lastFilteredV, filteredV;
double filteredI;
double offsetV, offsetI;
double phaseShiftedV;
double sqV,sumV,sqI,sumI,instP,sumP;
double realPower, apparentPower, powerFactor, Vrms, Irms;
bool lastVCross, checkVCross; 

// double I_RATIO = (long double)(CT_TURNS / CT_BURDEN_RESISTOR * 4.096 / 32768.0 * ICAL);
double I_RATIO = 2.048 / 32768.0 * ICAL;
double V_RATIO = (long double)(2.048 / 32768 * VCAL);

void init_ADC()
{
    adc.setGain(ADS1115_REG_CONFIG_PGA_2_048V); //* I will have to multiply gotten raw values by (4.096/32767) ~ 0.0625mV to get the voltage equivalent
    adc.setSampleRate(ADS1115_REG_CONFIG_DR_128SPS);

    if (!adc.testConnection()) {
      Serial.println("ADS1115 Connection failed");
      return;
    }
}

int read_current_raw()
{
    // Serial.print("Setting mux to differential mode between AIN0 and AIN01 @250SPS...");
    adc.setMux(ADS1115_REG_CONFIG_MUX_DIFF_0_1);
    adc.triggerConversion(); 
    int raw_value = adc.getConversion();
    return raw_value;
}

int read_voltage_raw()
{
    // Serial.print("Setting mux to differential mode between AIN02 and AIN03 @250SPS...");
    adc.setMux(ADS1115_REG_CONFIG_MUX_DIFF_2_3);
    adc.triggerConversion(); 
    int raw_value = adc.getConversion();
    return raw_value;
}

int calc_VI(unsigned int crossings, unsigned int timeout)		// typically use crosscount 10 && timeout 250
{
    unsigned int crossCount = 0;				//Used to measure number of times threshold is crossed.
    unsigned int numberOfSamples = 0;

	// 1) Waits for the waveform to be close to 'zero' (mid-scale adc) part in sin curve.
	unsigned long start = millis();
	while(1)
	{
		startV = read_voltage_raw();			//using the voltage waveform
		if ((startV < (ADC_COUNTS*0.55)) && (startV > (ADC_COUNTS*0.45))) break;  //check its within range
		if ((millis()-start)>timeout) break;
	}

	// 2) Main measurement loop
	start = millis();
	while ((crossCount < crossings) && ((millis()-start)<timeout)) 
	{
		numberOfSamples++;                       //Count number of times looped.
		lastFilteredV = filteredV;               //Used for delay/phase compensation
		lastsampleV = sampleV;
		
		// A) Read in raw voltage and current samples
		sampleV = read_voltage_raw();
		sampleI = read_current_raw();			

		// B) Apply digital low pass filters to extract the 2.5 V or 1.65 V dc offset,
    			//     then subtract this - signal is now centred on 0 counts.
		// offsetV = offsetV + ((sampleV-offsetV)/32768);
		// filteredV = sampleV - offsetV;
		filteredV = sampleV;
		// filteredV = 0.9989 * (lastFilteredV+sampleV-lastsampleV);
		// offsetI = offsetI + ((sampleI-offsetI)/32768);
		// filteredI = sampleI - offsetI;
		filteredI = sampleI;

		// C) Root-mean-square method voltage
		sqV= filteredV * filteredV;                 //1) square voltage values
    	sumV += sqV;                                //2) sum

		// D) Root-mean-square method current
		sqI = filteredI * filteredI;                //1) square current values
    	sumI += sqI;                                //2) sum

		// E) Phase calibration
		phaseShiftedV = lastFilteredV + PHASECAL * (filteredV - lastFilteredV);

		// F) Instantaneous power calc
		instP = phaseShiftedV * filteredI;          //Instantaneous Power
    	sumP +=instP;                               //Sum

		// G) Find the number of times the voltage has crossed the initial voltage
		//    - every 2 crosses we will have sampled 1 wavelength
		//    - so this method allows us to sample an integer number of half wavelengths which increases accuracy
		lastVCross = checkVCross;
		if (sampleV > startV) checkVCross = true;
						else checkVCross = false;
		if (numberOfSamples==1) lastVCross = checkVCross;

		if (lastVCross != checkVCross) crossCount++;
	}

	// 3) Post loop calculations
	//Calculation of the root of the mean of the voltage and current squared (rms)
  	//Calibration coefficients applied.

  	Vrms = V_RATIO * sqrt(sumV / numberOfSamples);
	Irms = I_RATIO * sqrt(sumI / numberOfSamples);

	//Calculation power values
	realPower = V_RATIO * I_RATIO * sumP / numberOfSamples;
	apparentPower = Vrms * Irms;
	powerFactor=realPower / apparentPower;

	//Reset accumulators
	sumV = 0;
	sumI = 0;
	sumP = 0;

	return realPower;
}

void serialprint()
{
	Serial.print(realPower);
	Serial.print(' ');
	Serial.print(apparentPower);
	Serial.print(' ');
	Serial.print(Vrms);
	Serial.print(' ');
	Serial.print(Irms);
	Serial.print(' ');
	Serial.print(powerFactor);
	Serial.println(' ');
	// delay(100);
}