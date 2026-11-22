int RunAnsiSequenceParserTests();
int RunOscHyperlinkTests();
int RunTerminalCellModelTests();
int RunEncodedTextDecoderTests();

int RunPttFilterTests();

int main()
{
    const int failures = RunAnsiSequenceParserTests() | RunOscHyperlinkTests()
        | RunPttFilterTests() | RunTerminalCellModelTests() | RunEncodedTextDecoderTests();
    return failures ? 1 : 0;
}
