#if 0
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioSynthWaveform       Tone_generator; //xy=159,576
AudioInputI2S            Bluetooth_in;   //xy=163,511
AsyncAudioInputSPDIF3    Optical_in;     //xy=167,446
AudioSynthNoisePink      pink1;          //xy=173,639
AudioMixer4              Right_mixer;    //xy=430,614
AudioMixer4              Left_mixer;     //xy=434,534
AudioFilterBiquad        Left_EQ;        //xy=692,527
AudioFilterBiquad        Right_EQ;       //xy=695,618
AudioMixer4              Mono;           //xy=879.5000114440918,573.5000076293945
AudioFilterBiquad        Left_highpass_1;        //xy=1060.250015258789,453.0000066757202
AudioFilterBiquad        Sub_lowpass_1;        //xy=1061.250015258789,572.5000085830688
AudioFilterBiquad        Right_highpass_1;        //xy=1066.250015258789,673.7500095367432
AudioFilterBiquad        Left_highpass_2;        //xy=1261.250015258789,452.5000066757202
AudioFilterBiquad        Sub_lowpass_2;        //xy=1261.250015258789,573.7500095367432
AudioFilterBiquad        Right_highpass_2;        //xy=1267.5000190734863,673.7500095367432
AudioFilterFIR           Sub_FIR_Filter; //xy=1533,595
AudioFilterFIR           Left_FIR_Filter; //xy=1535,457
AudioFilterFIR           Right_FIR_Filter; //xy=1539,730
AudioEffectDelay         Left_delay;     //xy=1762,454
AudioEffectDelay         Sub_delay;      //xy=1764,593
AudioEffectDelay         Right_delay;    //xy=1767,735
AudioAmplifier           Sub_volume;           //xy=2008.0000305175781,620.5000076293945
AudioOutputSPDIF3        LR_Spdif_Out;        //xy=2193,297
AudioOutputI2S           LR_Analog_Out;           //xy=2194,373
AudioOutputI2S2          Sub_Analog_Out;        //xy=2205.500030517578,619.0000081062317
AudioConnection          patchCord1(Tone_generator, 0, Left_mixer, 2);
AudioConnection          patchCord2(Tone_generator, 0, Right_mixer, 2);
AudioConnection          patchCord3(Bluetooth_in, 0, Left_mixer, 0);
AudioConnection          patchCord4(Bluetooth_in, 1, Right_mixer, 0);
AudioConnection          patchCord5(Optical_in, 0, Left_mixer, 1);
AudioConnection          patchCord6(Optical_in, 1, Right_mixer, 1);
AudioConnection          patchCord7(pink1, 0, Left_mixer, 3);
AudioConnection          patchCord8(pink1, 0, Right_mixer, 3);
AudioConnection          patchCord9(Right_mixer, 0, Right_EQ, 0);
AudioConnection          patchCord10(Left_mixer, 0, Left_EQ, 0);
AudioConnection          patchCord11(Left_EQ, 0, Mono, 0);
AudioConnection          patchCord12(Left_EQ, 0, Left_highpass_1, 0);
AudioConnection          patchCord13(Right_EQ, 0, Mono, 1);
AudioConnection          patchCord14(Right_EQ, 0, Right_highpass_1, 0);
AudioConnection          patchCord15(Mono, 0, Sub_lowpass_1, 0);
AudioConnection          patchCord16(Left_highpass_1, 0, Left_highpass_2, 0);
AudioConnection          patchCord17(Sub_lowpass_1, 0, Sub_lowpass_2, 0);
AudioConnection          patchCord18(Right_highpass_1, 0, Right_highpass_2, 0);
AudioConnection          patchCord19(Left_highpass_2, 0, Left_FIR_Filter, 0);
AudioConnection          patchCord20(Sub_lowpass_2, 0, Sub_FIR_Filter, 0);
AudioConnection          patchCord21(Right_highpass_2, 0, Right_FIR_Filter, 0);
AudioConnection          patchCord22(Sub_FIR_Filter, 0, Sub_delay, 0);
AudioConnection          patchCord23(Left_FIR_Filter, 0, Left_delay, 0);
AudioConnection          patchCord24(Right_FIR_Filter, 0, Right_delay, 0);
AudioConnection          patchCord25(Left_delay, 0, LR_Spdif_Out, 0);
AudioConnection          patchCord26(Left_delay, 0, LR_Analog_Out, 0);
AudioConnection          patchCord27(Sub_delay, 0, Sub_volume, 0);
AudioConnection          patchCord28(Right_delay, 0, LR_Spdif_Out, 1);
AudioConnection          patchCord29(Right_delay, 0, LR_Analog_Out, 1);
AudioConnection          patchCord30(Sub_volume, 0, Sub_Analog_Out, 0);
// GUItool: end automatically generated code
#endif