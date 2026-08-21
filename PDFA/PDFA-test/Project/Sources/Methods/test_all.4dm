//%attributes = {"invisible":true}
var $inputPath : Text
var $outputPath : Text
var $result : Object

$inputPath:=Temporary folder+"test_input.pdf"
$outputPath:=Temporary folder+"test_output_pdfa3.pdf"

// Test with non-existent file (should return error object)
$result:=PDF TO PDFA($inputPath; $outputPath)
ASSERT($result#Null; "PDF TO PDFA should return an object")
ASSERT($result.success=False; "Should fail with non-existent input")

// Test with empty params
$result:=PDF TO PDFA(""; "")
ASSERT($result#Null; "Should return object for empty params")
ASSERT($result.success=False; "Should fail with empty params")

KILL WORKER
