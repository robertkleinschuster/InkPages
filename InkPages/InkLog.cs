namespace InkPages;

/// <summary>
/// Where the library logs (exceptions are logged before they propagate, never swallowed): the app sets
/// <see cref="Writer"/> to its own log (by default the debugger's output) and, to tell errors apart,
/// <see cref="ErrorWriter"/> (by default the same as <see cref="Writer"/>).
/// </summary>
public static class InkLog
{
    public static Action<string> Writer { get; set; } = message => System.Diagnostics.Debug.WriteLine(message);

    /// <summary>Where errors go: exceptions (<see cref="Error(string, Exception)"/>) and <see cref="Error(string)"/>; null: <see cref="Writer"/>.</summary>
    public static Action<string>? ErrorWriter { get; set; }

    /// <summary>What the library did (gestures, undo steps).</summary>
    public static void Write(string message) => Writer(message);

    /// <summary>Something went wrong without an exception (e.g. the clipboard's ink added no strokes).</summary>
    public static void Error(string message) => (ErrorWriter ?? Writer)(message);

    /// <summary>An exception completely: type, HResult, message, stack.</summary>
    public static void Error(string context, Exception e) =>
        Error($"{context}: {e.GetType().FullName} 0x{e.HResult:X8} {e}");

    /// <summary>
    /// Runs an event handler and logs its exception before it propagates (never swallowed): for events whose exceptions
    /// would not reach the app's UnhandledException (WinUI's forwarded ink events) or need their context in the log.
    /// </summary>
    public static void Guard(string context, Action handler)
    {
        try
        {
            handler();
        }
        catch (Exception e)
        {
            Error(context, e);
            throw;
        }
    }
}
