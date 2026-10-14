using Rug.Constants;
using System;
using System.IO;

namespace Rug;

/// <summary>
/// Saves and restores the rug's exact draped shape: the scale it was built at and every cloth vertex position.
/// A restart can then put the rug back with its folds intact instead of dropping a fresh one.
/// </summary>
internal static class DrapeStore
{
    private static string FilePath => Path.Combine(AppSettings.DataDir, DrapeConstants.FileName);

    /// <summary>Writes the drape: its scale and the first <paramref name="count"/> vertices of <paramref name="points"/>.</summary>
    /// <param name="scale">The rug scale the drape was built at, so a restart rebuilds the cloth to the same size.</param>
    /// <param name="points">Vertex positions, three floats (x, y, z) per vertex; may be longer than needed.</param>
    /// <param name="count">The vertex count to write.</param>
    public static void Save(float scale, float[] points, int count)
    {
        if (count <= 0 || points.Length < count * 3) return;
        try
        {
            Directory.CreateDirectory(AppSettings.DataDir);
            using var w = new BinaryWriter(File.Create(FilePath));
            w.Write(DrapeConstants.Magic);
            w.Write(DrapeConstants.Version);
            w.Write(count);
            w.Write(scale);
            for (int i = 0; i < count * 3; i++) w.Write(points[i]);
        }
        catch (Exception)
        {
        }
    }

    /// <summary>Reads a saved drape. Returns false, with an empty array, when there is none or it is unreadable.</summary>
    /// <param name="scale">The scale the drape was saved at.</param>
    /// <param name="points">Vertex positions, three floats per vertex.</param>
    /// <returns>True when a valid drape was read.</returns>
    public static bool TryLoad(out float scale, out float[] points)
    {
        scale = 0;
        points = [];
        try
        {
            if (!File.Exists(FilePath)) return false;
            using var r = new BinaryReader(File.OpenRead(FilePath));
            if (r.ReadInt32() != DrapeConstants.Magic || r.ReadInt32() != DrapeConstants.Version) return false;
            int count = r.ReadInt32();
            scale = r.ReadSingle();
            if (count <= 0 || count > 1_000_000) return false;
            var p = new float[count * 3];
            for (int i = 0; i < count * 3; i++) p[i] = r.ReadSingle();
            points = p;
            return true;
        }
        catch (Exception)
        {
            scale = 0;
            points = [];
            return false;
        }
    }
}
