/**
 * @file main.cpp
 * @authors Christopher Rothmann (chrisrothmann@gatech.edu) & Luke Fink (lfink6@gatech.edu)
 * @brief C++ Code to create an MP3 player from an mBED
 * @version 1.0
 * @date 2021-12-13
 * 
 * @copyright Copyright (c) 2021
**/


// Define included libraries; all libraries below must be compiled together
// Note: Some libraries have been updated to work with this code. Ensure all libraries 
// are the correct by using those included in this github
#include "mbed.h"
#include "rtos.h"
#include "SDFileSystem.h"
#include "uLCD_4DGL.h"
#include "wave_player.h"
#include "MMA8452.h"
#include "PinDetect.h"
#include <string>
#include <vector>

// Defining mBED inputs & outputs

// mBED LED Outputs for Audiovisualizer/Testing & Diagnostics
DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

// Pushbuttons for MP3 Player Controls
PinDetect prev(p21);
PinDetect next(p22);
PinDetect shuffle(p23);
PinDetect play(p24);

// Serial & Analog Inputs & Ouputs for Data Communication
RawSerial blueTooth(p28,p27);
Serial pc(USBTX, USBRX);
SDFileSystem sd(p5, p6, p7, p12, "sd");
uLCD_4DGL uLCD(p13,p14,p11);
MMA8452 acc(p9, p10, 100000);
AnalogOut DACout(p18);
wave_player waver(&DACout);


// Defining Internal Global Variables
bool playing = false;
int currentSong = 0;
int songCount = 0;
vector<string> songList;
unsigned short max_range = 0xFFFF;

// Defining Functions

/**
 * @brief Increments integer variable currentSong by one, while circling back to first song at end of list
 * @details Function is called both when "next song" pushbutton pressed or bluetooth command is sent;
 * LED1 switches value when called for diagnostics & testing
**/
void nextSong()
{
    //led1 = !led1;
    if (currentSong == songCount - 1)
    {
        currentSong = 0;
    }
    else
    {
        currentSong++;
    }
}

/**
 * @brief Increments integer variable currentSong by minus one, while circling back to last song at zero
 * @details Function is called both when "previous song" pushbutton pressed or bluetooth command is sent;
 * LED2 switches value when called for diagnostics & testing
**/
void prevSong()
{
    //led2 = !led2;
    if (currentSong == 0)
    {
        currentSong = songCount - 1;
    }
    else
    {
        currentSong--;
    }
}


/**
 * @brief Switches boolean variable playing
 * @details Function is called both when "pause/play" pushbutton pressed or bluetooth command is sent;
 * LED3 switches value when called for diagnostics & testing
**/
void playSong()
{
    //led3 = !led3;
    playing = !playing;
}

/**
 * @brief Generates random integer within song list range to assign integer variable currentSong
 * @details Function is called both when "shuffle song" pushbutton pressed or bluetooth command is sent;
 * function seeds a true random value through the noise present on the 5th decimal place of an
 * accelerometer's input values;
 * LED4 switches value when called for diagnostics & testing
**/
void shuffleSong()
{
    //led4 = !led4;
    double x, y, z;
    acc.readXYZGravity(&x,&y,&z);
    currentSong = int(100000 * (x + y + z)) % songCount;
}

// Defining Threads

/**
 * @brief Updates LCD screen according to user input & selections
 * @details First configures LCD screen layout & songlist, then continously checks for changes in global variables
 * integer currentSong & boolean playing to update LCD screen accordingly. No updates made if no changes found.
 * All LCD communications occur strictly in this thread.
 * @param *arguments Input arguments to thread used for RTOS thread library. Not needed to understand thread code.
 */
void LCDThread(void const *argument)
{
    // Configure LCD screen
    uLCD.cls();
    uLCD.baudrate(3000000);
    uLCD.background_color(BLACK);
    uLCD.color(WHITE);
    uLCD.text_width(1);
    uLCD.text_height(1);   

    // Print Song List to LCD Screen
    uLCD.locate(0,0);
    uLCD.printf("Song List: ");
    uLCD.locate(0,1);
    uLCD.printf("->");
    for(int i = 0; i < songCount; i++)
    {
        uLCD.locate(3,i+1);
        uLCD.printf("%s\n\r", songList[i].substr(0,songList[i].find(".wav")));
    }
    
    // Print "NOW PLAYING: " & "STATUS: " feature; initialize to first song on SD card & paused
    uLCD.locate(0,12);
    uLCD.printf("NOW PLAYING:");
    uLCD.locate(0,13);
    uLCD.printf("%s", songList[currentSong].substr(0,songList[currentSong].find(".wav")));
    uLCD.locate(0,14);
    uLCD.printf("STATUS: PAUSED");

    // Initialize internal thread variables to check for changes to external global variables
    bool prevPlayLCD = false;
    int previousSongLCD = 0;

    // Thread while loop to continously check for changes and update screen accordingly
    while (true)
    {   
        // Check if new song has been selected
        if (previousSongLCD != currentSong)
        {
            // Update "NOW PLAYING: " feature
            uLCD.locate(0,12);
            uLCD.printf("NOW PLAYING:");
            uLCD.locate(0,13);
            uLCD.printf("%s   ", songList[currentSong].substr(0,songList[currentSong].find(".wav")));
            // Update "->" feature
            uLCD.locate(0, previousSongLCD + 1);
            uLCD.printf("  ");
            uLCD.locate(0, currentSong + 1);
            uLCD.printf("->");
            // Set internal change check to currentSong
            previousSongLCD = currentSong;
        }
        //Check if change to play/pause status
        if (prevPlayLCD != playing)
        {
            // Update "STATUS: " feature
            uLCD.locate(0,14);
            if (playing)
            {
                uLCD.printf("STATUS: PLAYING");
            }
            else
            {
                uLCD.printf("STATUS: PAUSED ");
            }
            // Set internal change check to playing
            prevPlayLCD = playing;
        }
        Thread::wait(50);
    }
}

/**
 * @brief Updates phone screen to latest currentSong playing, sends phone commands to mBED, all over BlueTooth
 * @details See commenting in thread for step-by-step approach
 * All BlueTooth communications occur strictly in this thread
 * BlueTooth Control Pad Module Controls:  1 = Pause/Play, 2 = Next Song, 3 = Previous Song, 4 = Shuffle Song
 * @param *arguments Input arguments to thread used for RTOS thread library. Not needed to understand thread code.
 */
void BluetoothThread(void const *argument)
{
    // Initialize internal thread variable to check for changes to external global variables
    int previousSongBLE = 0;
    // Thread while look to continously check for BlueTooth commands and update currentSong on phone
    while (true)
    {
        // Update currentSong on phone
        if (blueTooth.writeable())
        {
            // Check if new song has been selected
            if (previousSongBLE != currentSong)
            {
                // Send currentSong name over BlueTooth
                string str = "Current Song: ";
                for (int i = 0; i < 14; i++)
                {
                    blueTooth.putc(str[i]);
                }
                for (int i = 0; i < songList[currentSong].size() - 4; i++)
                {
                    blueTooth.putc(songList[currentSong][i]);
                }
                blueTooth.putc('\n');
                previousSongBLE = currentSong;
            }
            
        }
        // Read in commands from BlueTooth module
        if (blueTooth.readable())
        {
            // Check for '!B' to be compatible with "Control Pad" Module serial output
            if (blueTooth.getc()=='!')
            {
                if (blueTooth.getc()=='B')
                {
                    // Check which command was hit
                    char bnum = blueTooth.getc();
                    // Ensure mBED only updates on release, not hit
                    char bhit = blueTooth.getc();
                    if (bhit == '0')
                    {
                        switch (bnum)
                            {
                                case '1':
                                playSong();
                                break;
                                
                                case '2':
                                nextSong();
                                break;
                                
                                case '3':
                                prevSong();
                                break;
                                
                                case '4':
                                shuffleSong();
                                break;
                                
                                default:
                                break;
                            }
                    }
                }
            }
        }
        Thread::wait(50);
    }
}

/**
 * @brief Updates Mbed LEDs to show current volume level 
 * @details Read and scales analogOut level, then sets leds to show the level in 4 tiers. 
 * @param *arguments Input arguments to thread used for RTOS thread library. Not needed to understand thread code.
 */
void AudioVisualizerThread(void const *argument)
{
        while(1)
        {
            if(playing)
            {
                float level = (DACout.read() - 0.25f) * 3.3f;
                if(level<0.825)
                {
                    led1=true;
                    led2=led3=led4=false;
                }
                else if(level>0.825&&level<1.65)
                {
                    led1=led2=true;
                    led3=led4=false;
                }
                else if(level>1.65&&level<2.47)
                {
                    led1=led2=led3=true;
                    led4=false;
                }
                else if(level>2.47)
                {
                    led1=led2=led3=led4=true;
                }
                Thread::wait(50);
            }
        }
}

// Button Interupt Functions

/**
 * @brief runs nextSong() function on pushbotton hit. Attached using PinDetect.
**/
void nextInt()
{
    nextSong();
}

/**
 * @brief runs prevSong() function on pushbotton hit. Attached using PinDetect.
**/
void prevInt()
{
    prevSong();
}

/**
 * @brief runs playSong() function on pushbotton hit. Attached using PinDetect.
**/
void playInt()
{
    playSong();
}

/**
 * @brief runs shuffleSong() function on pushbotton hit. Attached using PinDetect.
**/
void shuffleInt()
{
    shuffleSong();
}

/**
 * @brief Program main routine.
 * @return int No return expected.
 */
int main()
{   
    // Attach & configure interupts to pushbuttons
    next.mode(PullUp);
    prev.mode(PullUp);
    play.mode(PullUp);
    shuffle.mode(PullUp);
    next.attach_deasserted(&nextInt);
    prev.attach_deasserted(&prevInt);
    play.attach_deasserted(&playInt);
    shuffle.attach_deasserted(&shuffleInt);
    next.setSampleFrequency();
    prev.setSampleFrequency();
    play.setSampleFrequency();
    shuffle.setSampleFrequency();
    // Wait 10 milliseconds to ensure functions are attached
    Thread::wait(10);
    
    // Extract file list from SD Card, place in vector<string> songList
    DIR *dp;
    struct dirent *dirp;
    dp = opendir("/sd/myMusic");
    if(dp !=NULL)
    {
       while ((dirp = readdir(dp)) != NULL) {
            songList.push_back(string(dirp->d_name));
            songCount++;
        }
    }
    // Wait 10 miliseconds to ensure SD card communication complete
    Thread::wait(1000);
    
    // Start LCD & BlueTooth Thread
    Thread thread1(LCDThread);
    Thread thread2(BluetoothThread);
    Thread thread3(AudioVisualizerThread);

    // Main while loop:
    // Main loop is now considered the Speaker Thread, playing/pausing current song 
    // based on changes in global varaibles boolean playing & integer currentSong
    while (true)
    {
        // Read in selected file
        FILE *wave_file;
        string selectedSong= "/sd/myMusic/" + songList[currentSong];
        const char* song = selectedSong.c_str();
        wave_file=fopen(song,"r");
        if(wave_file==NULL)
        {
            uLCD.locate(0,12);
            uLCD.printf("file open error!");
        }
        // Wait 10 miliseconds to ensure file properly loaded
        Thread::wait(1000);
        // Play file; stop/play feature built into waver library
        waver.play(wave_file);
        // Close file
        fclose(wave_file);
        // Reset playing variable so song does not repeat
        playing = false;
    }
}
