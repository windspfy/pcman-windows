int RunAnsiSequenceParserTests();
int RunOscHyperlinkTests();
int RunTerminalCellModelTests();
int RunEncodedTextDecoderTests();

int main()
{
    const int failures = RunAnsiSequenceParserTests() | RunOscHyperlinkTests()
        | RunTerminalCellModelTests() | RunEncodedTextDecoderTests();
    return failures ? 1 : 0;
}
