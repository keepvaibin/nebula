using System;
using System.Globalization;

namespace Nebula
{
    /// <summary>Resolved output, content and internal-rendering dimensions.</summary>
    internal sealed class DisplayPlan
    {
        public int OutputWidth, OutputHeight;
        public string Aspect;
        public double AspectRatio;
        public bool NativeFourThree;
        public string InternalSelection;
        public int TargetWidth, TargetHeight;
        public int EfbScale;
        public int SceneWidth, SceneHeight, BackingWidth, BackingHeight;
        public int ContentLeft, ContentTop, ContentWidth, ContentHeight;
    }

    /// <summary>
    /// Display geometry shared by the launcher and its tests. Output presets
    /// are a height times the selected aspect, rounded to an even width; the
    /// internal EFB uses the smallest integer scale that covers the target.
    /// </summary>
    internal static class DisplaySettings
    {
        /// <summary>Must equal galaxy::gx::kMaxEfbScale in render_config.h.</summary>
        public const int MaxEfbScale = 16;
        public const int MinWidth = 320, MaxWidth = 7680, MinHeight = 240, MaxHeight = 4320;
        private const int NativeSceneWidth = 640, NativeSceneHeight = 456;
        private const int EfbWidth = 640, EfbHeight = 528;

        public static readonly string[] Presets = { "480p", "720p", "1080p", "1440p", "2K / QHD (1440p)", "4K (2160p)" };
        public static readonly string[] Aspects = { "4:3", "16:9", "16:10", "21:9", "32:9" };

        public static double AspectRatio(string aspect)
        {
            var culture = CultureInfo.InvariantCulture;
            string[] parts = (aspect ?? "").Split(':');
            double ratio;
            if (parts.Length == 2)
            {
                double numerator = double.Parse(parts[0], culture), denominator = double.Parse(parts[1], culture);
                if (denominator <= 0) throw new ArgumentException("Aspect denominator must be positive.");
                ratio = numerator / denominator;
            }
            else if (parts.Length == 1) ratio = double.Parse(aspect, culture);
            else throw new ArgumentException("Use an aspect such as 4:3, 16:9, 16:10 or 21:9.");
            if (double.IsNaN(ratio) || double.IsInfinity(ratio) || ratio < 4.0 / 3.0 - 1e-12 || ratio > 32.0 / 9.0 + 1e-12)
                throw new ArgumentException("Nebula supports aspects from 4:3 through 32:9.");
            return ratio;
        }

        public static int PresetHeight(string preset)
        {
            switch (preset)
            {
                case "480p": return 480;
                case "720p": return 720;
                case "1080p": return 1080;
                case "1440p":
                case "2K / QHD (1440p)": return 1440;
                case "4K (2160p)": return 2160;
            }
            throw new ArgumentException("Select 480p, 720p, 1080p, 1440p, 2K / QHD (1440p) or 4K (2160p).");
        }

        public static int EvenWidth(int height, double ratio)
        {
            return (int)(2 * Math.Round(height * ratio / 2, MidpointRounding.AwayFromZero));
        }

        public static void OutputPreset(string preset, string aspect, out int width, out int height)
        {
            height = PresetHeight(preset);
            width = EvenWidth(height, AspectRatio(aspect));
            if (width < MinWidth || width > MaxWidth)
                throw new ArgumentException("This output preset exceeds the supported window width (320-7680 pixels).");
        }

        /// <param name="internalSelection">"Match Output", "Custom", or a height: 480, 720, 1080, 1440, 2160.</param>
        public static DisplayPlan Resolve(int outputWidth, int outputHeight, string aspect, string internalSelection,
            int customWidth, int customHeight)
        {
            if (outputWidth < MinWidth || outputWidth > MaxWidth || outputHeight < MinHeight || outputHeight > MaxHeight)
                throw new ArgumentException("Output size must be between 320x240 and 7680x4320.");
            double ratio = AspectRatio(aspect);
            var plan = new DisplayPlan();
            plan.OutputWidth = outputWidth;
            plan.OutputHeight = outputHeight;
            plan.Aspect = aspect;
            plan.AspectRatio = ratio;
            plan.NativeFourThree = Math.Abs(ratio - 4.0 / 3.0) < 1e-10;
            plan.InternalSelection = internalSelection;
            plan.ContentWidth = outputWidth;
            plan.ContentHeight = outputHeight;
            if (outputWidth / (double)outputHeight > ratio)
                plan.ContentWidth = (int)Math.Round(outputHeight * ratio, MidpointRounding.AwayFromZero);
            else
                plan.ContentHeight = (int)Math.Round(outputWidth / ratio, MidpointRounding.AwayFromZero);
            plan.ContentLeft = (outputWidth - plan.ContentWidth) / 2;
            plan.ContentTop = (outputHeight - plan.ContentHeight) / 2;

            bool native480 = false;
            if (internalSelection == "Match Output")
            {
                plan.TargetWidth = plan.ContentWidth;
                plan.TargetHeight = plan.ContentHeight;
            }
            else if (internalSelection == "Custom")
            {
                if (customWidth < MinWidth || customHeight < MinHeight)
                    throw new ArgumentException("Custom internal dimensions must be at least 320 by 240.");
                plan.TargetHeight = customHeight;
                plan.TargetWidth = EvenWidth(customHeight, ratio);
                if (customWidth != plan.TargetWidth)
                    throw new ArgumentException(string.Format(
                        "Custom width must be {0} for this height and aspect (two-pixel alignment).", plan.TargetWidth));
            }
            else
            {
                int height;
                if (!int.TryParse(internalSelection, out height) || Array.IndexOf(new[] { 480, 720, 1080, 1440, 2160 }, height) < 0)
                    throw new ArgumentException("Select 480, 720, 1080, 1440, 2160, Match Output or Custom.");
                plan.TargetHeight = height;
                plan.TargetWidth = EvenWidth(height, ratio);
                native480 = height == 480;
            }
            plan.EfbScale = native480 ? 1 : (int)Math.Ceiling(Math.Max(
                plan.TargetWidth / (double)NativeSceneWidth, plan.TargetHeight / (double)NativeSceneHeight));
            if (plan.EfbScale < 1 || plan.EfbScale > MaxEfbScale)
                throw new ArgumentException(string.Format(
                    "The internal target {0} x {1} needs more than {2}x internal resolution. Choose a smaller internal target.",
                    plan.TargetWidth, plan.TargetHeight, MaxEfbScale));
            plan.SceneWidth = NativeSceneWidth * plan.EfbScale;
            plan.SceneHeight = NativeSceneHeight * plan.EfbScale;
            plan.BackingWidth = EfbWidth * plan.EfbScale;
            plan.BackingHeight = EfbHeight * plan.EfbScale;
            return plan;
        }
    }
}
