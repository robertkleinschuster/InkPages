namespace InkPages;

/// <summary>
/// Where the library logs (exceptions are logged before they propagate, never swallowed): the app sets
/// <see cref="Writer"/> to its own log (by default the debugger's output) and, to tell errors apart,
/// <see cref="ErrorWriter"/> (by default the same as <see cref="Writer"/>).
/// </summary>
public static class InkLog
{
    public static Action<string> Writer { get; set; } = message => System.Diagnostics.Debug.WriteLine(message);

    /// <summary>Where errors go: exceptions (<see cref="Write(string, Exception)"/>) and <see cref="Error"/>; null: <see cref="Writer"/>.</summary>
    public static Action<string>? ErrorWriter { get; set; }

    /// <summary>What the library did (gestures, undo steps).</summary>
    public static void Write(string message) => Writer(message);

    /// <summary>Something went wrong without an exception (e.g. the clipboard's ink added no strokes).</summary>
    public static void Error(string message) => (ErrorWriter ?? Writer)(message);

    /// <summary>An exception completely: type, HResult, message, stack (an error).</summary>
    public static void Write(string context, Exception e) =>
        Error($"{context}: {e.GetType().FullName} 0x{e.HResult:X8} {e}");
}
