using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Windows.Devices.Enumeration;
using Windows.Devices.Spi;
using Windows.Devices.Gpio;
using Windows.UI;
//using DisplayFont;
using System.Diagnostics;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media.Imaging;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.UI.Xaml;

namespace App1
{
    class AdaFruitTFT
    {
        private const string SPI_CONTROLLER_NAME = "SPI0";  /* For Raspberry Pi 2, use SPI0                             */
        private const int SPI_CHIP_SELECT_LINE = 0;       /* Line 0 maps to physical pin number 24 on the Rpi2        */
        private const int DATA_COMMAND_PIN = 25;          /* We use GPIO 25 since it's conveniently near the SPI pins */
        private const int PhysicalMaxWidth = 240;
        private const int PhysicalMaxHeight = 320;

        SpiDevice SpiDisplay;

        GpioController IoController;
        GpioPin DC;
        DispatcherTimer captureTimer = null;
        UIElement captureTarget = null;

        const int kResetDelay = 5;
        byte resetCommand = 0x1;
        byte clearDisplayCommand = 0x28;

        public enum Orientation
        {
            Portrait = 0,
            Landscape = 1,
            PortraitInverted = 2,
            LandscapeInverted = 3
        };


        public AdaFruitTFT()
        {

        }

        public int MaxWidth
        {
            get
            {
                if (orientation == Orientation.Portrait || orientation == Orientation.PortraitInverted)
                    return PhysicalMaxWidth;

                return PhysicalMaxHeight;
            }
        }

        public int MaxHeight
        {
            get
            {
                if (orientation == Orientation.Portrait || orientation == Orientation.PortraitInverted)
                    return PhysicalMaxHeight;

                return PhysicalMaxWidth;
            }
        }

        public async Task initialize()
        {
            try
            {
                var settings = new SpiConnectionSettings(SPI_CHIP_SELECT_LINE); /* Create SPI initialization settings                               */
                settings.ClockFrequency = 10000000;                             /* Datasheet specifies maximum SPI clock frequency of 10MHz         */
                settings.Mode = SpiMode.Mode3;                                  /* The display expects an idle-high clock polarity, we use Mode3
                                                                         * to set the clock polarity and phase to: CPOL = 1, CPHA = 1
                                                                         */

                string spiAqs = SpiDevice.GetDeviceSelector(SPI_CONTROLLER_NAME);       /* Find the selector string for the SPI bus controller          */
                var devicesInfo = await DeviceInformation.FindAllAsync(spiAqs);         /* Find the SPI bus controller device with our selector string  */
                SpiDisplay = await SpiDevice.FromIdAsync(devicesInfo[0].Id, settings);  /* Create an SpiDevice with our bus controller and SPI settings */

                IoController = GpioController.GetDefault(); /* Get the default GPIO controller on the system */

                /* Initialize a pin as output for the Data/Command line on the display  */
                DC = IoController.OpenPin(DATA_COMMAND_PIN);
                DC.Write(GpioPinValue.Low);
                DC.SetDriveMode(GpioPinDriveMode.Output);


            }
            /* If initialization fails, display the exception and stop running */
            catch (Exception ex)
            {
                throw new Exception("SPI Initialization Failed", ex);
            }

            // Startup Sequence
            sendC(resetCommand);
            await Task.Delay(kResetDelay);
            sendC(clearDisplayCommand);

            sendC(0xCF);
            sendD(new byte[] { 0x00, 0x83, 0x30 });
            sendC(0xED);
            sendD(new byte[] { 0x64, 0x03, 0x12, 0x81 });
            sendC(0xE8);
            sendD(new byte[] { 0x85, 0x01, 0x79 });
            sendC(0xCB);
            sendD(new byte[] { 0x39, 0X2C, 0x00, 0x34, 0x02 });
            sendC(0xF7);
            sendD(new byte[] { 0x20 });
            sendC(0xEA);
            sendD(new byte[] { 0x00, 0x00 });
            /* ------------power control-------------------------------- */
            sendC(0xC0);
            sendD(new byte[] { 0x26 });
            sendC(0xC1);
            sendD(new byte[] { 0x11 });
            /* ------------VCOM --------- */
            sendC(0xC5);
            sendD(new byte[] { 0x35, 0x3E });
            sendC(0xC7);
            sendD(new byte[] { 0xBE });
            /* ------------memory access control------------------------ */
            sendC(0x3A);
            sendD(new byte[] { 0x55 }); /* 16bit pixel */
            /* ------------frame rate----------------------------------- */
            sendC(0xB1);
            sendD(new byte[] { 0x00, 0x1B });
            /* ------------Gamma---------------------------------------- */
            /* sendD(new byte[] {0xF2, 0x08); */ /* Gamma Function Disable */
            sendC(0x26);
            sendD(new byte[] { 0x01 });
            /* ------------display-------------------------------------- */
            sendC(0xB7);
            sendD(new byte[] { 0x07 }); /* entry mode set */
            sendC(0xB6);
            sendD(new byte[] { 0x0A, 0x82, 0x27, 0x00 });
            sendC(0x11); /* sleep out */
            await Task.Delay(100);
            sendC(0x29); /* display on */
            await Task.Delay(20);

            this.orientation = Orientation.Portrait;

            captureTimer = new DispatcherTimer();
            captureTimer.Tick += CaptureTimer_Tick;
        }


        private Orientation currentOrientation = Orientation.Portrait;

        public Orientation orientation
        {
            get
            {
                return currentOrientation;
            }
            set
            {
                currentOrientation = value;
                sendC(0x36);
                switch (value)
                {
                    case Orientation.Portrait:
                        sendC(0x48);
                        break;
                    case Orientation.Landscape:
                        sendC(0x28);
                        break;
                    case Orientation.PortraitInverted:
                        sendC(0x88);
                        break;
                    case Orientation.LandscapeInverted:
                        sendC(0xE8);
                        break;
                }
            }
        }

        private void sendC(byte cmd)
        {
            DC.Write(GpioPinValue.Low);
            SpiDisplay.Write(new byte[] { cmd });
            DC.Write(GpioPinValue.High);
        }
        private void sendD(byte[] data)
        {
            SpiDisplay.Write(data);
        }

        public void setWindow(int x, int y, int w, int h)
        {
            var windowWidth = (x + w - 1);
            var windowHeight = (y + h - 1);
            sendC(0x2A);
            sendD(new byte[] { 
                (byte)((x >> 8) & 0xFF),
                (byte)(x & 0xFF),
                (byte)((windowWidth >> 8) & 0xFF),
                (byte)(windowWidth & 0xFF)
            });

            sendC(0x2B);
            sendD(new byte[] {
                (byte)((y >> 8) & 0xFF),
                (byte)(y & 0xFF),
                (byte)((windowHeight >> 8) & 0xFF),
                (byte)(windowHeight & 0xFF)
            });
        }

        public void setWindowMax()
        {
            setWindow(0, 0, MaxWidth, MaxHeight);
        }

        public byte[] getColorBytes(Color color)
        {
            int color565 = (((color.R & 0xF8) << 8) | ((color.G & 0xFC) << 3) | ((color.B & 0xF8) >> 3));
            return new byte[] { (byte)((color565 >> 8) & 0xFF), (byte)(color565 & 0xFF)};
        }

        public void writeColor(Color color)
        {
            sendD(getColorBytes(color));
        }

        public void fillRect(int x, int y, int w, int h, Color color)
        {
            setWindow(x, y, w, h);

            var numPixels = w * h;
            var numPixelBytes = numPixels * 2;
            var colorArray = getColorBytes(color);
            sendC(0x2C);

            byte[] framebuffer = new byte[numPixelBytes];
            for (int pixel = 0; pixel < framebuffer.Length; pixel += 2)
            {
                framebuffer[pixel] = colorArray[0];
                framebuffer[pixel + 1] = colorArray[1];
            }
            sendD(framebuffer);
        }

        public async Task Render(RenderTargetBitmap image)
        {
            var buffer = await image.GetPixelsAsync();
            var numPixels = image.PixelWidth * image.PixelHeight;
            var numPixelBytes = numPixels * 2;

            byte[] framebuffer = new byte[numPixelBytes];

            byte[] pixels = buffer.ToArray();
            for (int y = 0; y < image.PixelHeight; y++)
            {
                for (int x = 0; x < image.PixelWidth; x++)
                {
                    int destX = (y * image.PixelWidth + x) * 2;
                    int sourceX = (y * image.PixelWidth + (image.PixelWidth - x - 1)) * 4;

                    int color565 = (((pixels[sourceX + 0] & 0xF8) << 8) | ((pixels[sourceX + 1] & 0xFC) << 3) | ((pixels[sourceX + 2] & 0xF8) >> 3));

                    framebuffer[destX] = (byte)((color565 >> 8) & 0xFF);
                    framebuffer[destX + 1] = (byte)((color565 & 0xFF));
                }
            }

            setWindow(0, 0, image.PixelWidth, image.PixelHeight);
            sendC(0x2C);
            sendD(framebuffer);
        }


        public void capture(UIElement target, uint milli)
        {
            captureTarget = target;
            captureTimer.Interval = TimeSpan.FromMilliseconds(milli);
            captureTimer.Start();
        }
        private async void CaptureTimer_Tick(object sender, object e)
        {
            var renderBitmap = new RenderTargetBitmap();
            await renderBitmap.RenderAsync(captureTarget, MaxWidth, MaxHeight);
            await Render(renderBitmap);
        }
    }
}
