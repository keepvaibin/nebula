using System.Drawing;
using System.Windows.Forms;

namespace Nebula
{
    /// <summary>
    /// Forms are laid out in 96-DPI pixels while text renders at the display's
    /// DPI; scale the layout once to match.
    /// </summary>
    internal static class UiScale
    {
        public static void Apply(Form form)
        {
            float factor;
            using (var graphics = form.CreateGraphics()) factor = graphics.DpiX / 96f;
            if (factor > 1.01f) form.Scale(new SizeF(factor, factor));
        }
    }
}
