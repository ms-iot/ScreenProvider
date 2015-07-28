using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Media.Imaging;
using Windows.UI.Xaml.Navigation;
using Windows.Graphics.DirectX;
using System.Threading.Tasks;

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

namespace App1
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        AdaFruitTFT tft = new AdaFruitTFT();

        public MainPage()
        {
            this.InitializeComponent();
        }

        public async Task<RenderTargetBitmap> GetImage(UIElement target, int width, int height)
        {
            var renderBitmap = new RenderTargetBitmap();
            var scale = new ScaleTransform();
            scale.ScaleX = 2.0;
            var oldTransform = target.RenderTransform;
            target.RenderTransform = scale;
            await renderBitmap.RenderAsync(target, width, height);
            target.RenderTransform = oldTransform;
            return renderBitmap;
        }

        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            await tft.initialize();
            //tft.fillRect(0, 0, tft.MaxWidth, tft.MaxHeight, Color.FromArgb(0xFF, 0, 0, 0xFF));

            RenderTargetBitmap image = await GetImage(this, tft.MaxWidth, tft.MaxHeight);

            await tft.Render(image);
        }
    }
}
