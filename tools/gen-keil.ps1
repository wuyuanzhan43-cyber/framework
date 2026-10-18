<#
.SYNOPSIS
    Generate the Keil MDK (uVision) project for the STM32F407VE target.

.DESCRIPTION
    The framework keeps one source of truth for what belongs in a firmware build:
    projects/stm32f407/manifest.json. The CMake build reads it; this script reads
    the same file and emits projects/stm32f407/stm32f407ve.uvprojx, so the Keil
    target cannot drift away from the CMake target. The script fails loudly if a
    source declared in the manifest is missing from the Keil group list.

    Verified toolchain: Keil MDK 5.38 with ARM Compiler 5.06 update 5 (armcc /
    armasm). ARM Compiler 6 is deliberately NOT configured: the FreeRTOS port
    wired up here is the AC5 (RVDS) one. See projects/stm32f407/KEIL.md.

.PARAMETER OutputPath
    Where to write the .uvprojx. Defaults to the file next to manifest.json.

.PARAMETER Build
    Also run UV4 -b on the generated project to verify it compiles.

.PARAMETER Configuration
    Target name to build. Defaults to Debug.

.EXAMPLE
    pwsh -File tools/gen-keil.ps1 -Build
#>
[CmdletBinding()]
param(
    [string] $OutputPath,
    [switch] $Build,
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot     = Split-Path -Parent $PSScriptRoot
$projectDir   = Join-Path $repoRoot 'projects\stm32f407'
$manifestPath = Join-Path $projectDir 'manifest.json'
if (-not $OutputPath) { $OutputPath = Join-Path $projectDir 'stm32f407ve.uvprojx' }

if (-not (Test-Path $manifestPath)) { throw "manifest not found: $manifestPath" }
$manifest = Get-Content -Raw -Encoding UTF8 $manifestPath | ConvertFrom-Json

# ---------------------------------------------------------------- source groups
# Paths are repository-relative; they are rewritten relative to the project
# directory when written, so the .uvprojx survives being moved with the repo.
$groups = [ordered]@{
    'framework/core' = @(
        'framework/core/io.c'
        'framework/core/ring.c'
    )
    'framework/components/protocol' = @(
        'framework/components/protocol/protocol.c'
    )
    'framework/app' = @(
        'framework/app/demo_app.c'
        'framework/app/assets/avatar_128x160.c'
    )
    'framework/boards/stm32f407ve' = @(
        'framework/boards/stm32f407ve/board.c'
    )
    'framework/ports' = @(
        'framework/ports/stm32f4/gpio.c'
        'framework/ports/stm32f4/uart.c'
        'framework/ports/stm32f4/st7735.c'
        'framework/ports/freertos/runtime.c'
    )
    'third_party/FreeRTOS' = @(
        'third_party/freertos-src/tasks.c'
        'third_party/freertos-src/queue.c'
        'third_party/freertos-src/list.c'
        'third_party/freertos-src/portable/RVDS/ARM_CM4F/port.c'
    )
    'third_party/HAL' = @(
        'third_party/hal-src/Src/stm32f4xx_hal.c'
        'third_party/hal-src/Src/stm32f4xx_hal_cortex.c'
        'third_party/hal-src/Src/stm32f4xx_hal_rcc.c'
        'third_party/hal-src/Src/stm32f4xx_hal_rcc_ex.c'
        'third_party/hal-src/Src/stm32f4xx_hal_gpio.c'
        'third_party/hal-src/Src/stm32f4xx_hal_uart.c'
        'third_party/hal-src/Src/stm32f4xx_hal_spi.c'
        'third_party/hal-src/Src/stm32f4xx_hal_dma.c'
        'third_party/hal-src/Src/stm32f4xx_hal_dma_ex.c'
        'third_party/hal-src/Src/stm32f4xx_hal_flash.c'
        'third_party/hal-src/Src/stm32f4xx_hal_flash_ex.c'
        'third_party/hal-src/Src/stm32f4xx_hal_pwr.c'
        'third_party/hal-src/Src/stm32f4xx_hal_pwr_ex.c'
    )
    'third_party/Device' = @(
        'third_party/device-src/Source/Templates/system_stm32f4xx.c'
    )
    'third_party/Startup' = @(
        'third_party/device-src/Source/Templates/arm/startup_stm32f407xx.s'
    )
    'project/stm32f407' = @(
        'projects/stm32f407/main.c'
        'projects/stm32f407/interrupts.c'
    )
}

$grouped  = @($groups.Values | ForEach-Object { $_ } | ForEach-Object { $_ -replace '/', '\' })
$coreSources = @($manifest.coreLibraries.fw_core) + @($manifest.coreLibraries.fw_protocol) + @($manifest.coreLibraries.fw_app)
$required = @($coreSources + $manifest.firmwareSources) | ForEach-Object { $_ -replace '/', '\' }

$missing = @($required | Where-Object { $grouped -notcontains $_ })
if ($missing.Count -gt 0) {
    throw ("manifest declares sources that no Keil group contains:`n  " + ($missing -join "`n  "))
}
foreach ($rel in $grouped) {
    if (-not (Test-Path (Join-Path $repoRoot $rel))) { throw "grouped source does not exist: $rel" }
}

# ------------------------------------------------------------------ build inputs
$defines = @($manifest.defines)
$includes = @($manifest.includes) + @('third_party/freertos-src/portable/RVDS/ARM_CM4F')
foreach ($inc in $includes) {
    if (-not (Test-Path (Join-Path $repoRoot $inc))) { throw "include path does not exist: $inc" }
}

# -------------------------------------------------------------- xml helpers
function ConvertTo-XmlText([string] $value) {
    $value.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;')
}

# .uvprojx paths are conventionally relative to the project directory.
function Get-RelativeToProject([string] $repoRelative) {
    '..\..\' + ($repoRelative -replace '/', '\')
}

# uVision stores FileType as a numeric code, NOT the file extension. Getting this
# wrong is silent: the project loads, the files appear in the tree, uVision even
# writes them into the .dep file, but a build compiles nothing and reports success.
#   1 = C source        2 = assembler source        5 = C++ source
function Get-FileTypeCode([string] $repoRelative) {
    switch ([System.IO.Path]::GetExtension($repoRelative).ToLowerInvariant()) {
        '.c'   { 1 }
        '.s'   { 2 }
        '.asm' { 2 }
        '.cpp' { 5 }
        default { throw "unsupported source extension: $repoRelative" }
    }
}

$defineAttr    = ($defines -join ';')
$includeAttr   = (($includes | ForEach-Object { Get-RelativeToProject $_ }) -join ';')

# Scatter file and device pack come from the manifest so the Keil target and the
# CMake target stay driven by the same description of the firmware.
if (-not $manifest.PSObject.Properties['keilScatter']) { throw 'manifest.json is missing "keilScatter"' }
if (-not $manifest.PSObject.Properties['keilPack'])    { throw 'manifest.json is missing "keilPack"' }
$scatterRepoPath = $manifest.keilScatter
if (-not (Test-Path (Join-Path $repoRoot $scatterRepoPath))) { throw "keilScatter not found: $scatterRepoPath" }
$scatterAttr = Get-RelativeToProject $scatterRepoPath
$packId      = $manifest.keilPack

# ------------------------------------------------------------------ render
$sb = [System.Text.StringBuilder]::new()
function Add-Line([string] $line) { $null = $sb.AppendLine($line) }

Add-Line '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>'
Add-Line '<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">'
Add-Line ''
Add-Line '  <SchemaVersion>2.1</SchemaVersion>'
Add-Line ''
Add-Line '  <Header>### uVision Project, (C) Keil Software</Header>'
Add-Line ''
Add-Line '  <Targets>'
Add-Line '    <Target>'
Add-Line "      <TargetName>$Configuration</TargetName>"
Add-Line '      <ToolsetNumber>0x4</ToolsetNumber>'
Add-Line '      <ToolsetName>ARM-ADS</ToolsetName>'
Add-Line '      <pCCUsed>5060528::V5.06 update 5 (build 528)::ARMCC</pCCUsed>'
Add-Line '      <uAC6>0</uAC6>'
Add-Line '      <TargetOption>'
Add-Line '        <TargetCommonOption>'
Add-Line '          <Device>STM32F407VETx</Device>'
Add-Line '          <Vendor>STMicroelectronics</Vendor>'
Add-Line "          <PackID>$packId</PackID>"
Add-Line '          <PackURL>https://www.keil.com/pack/</PackURL>'
# CLOCK must match the real core clock so debugger timing and flash programming
# delays are right. board_init() configures HSI with no PLL, i.e. 16 MHz.
Add-Line '          <Cpu>IRAM(0x20000000,0x00020000) IROM(0x08000000,0x00080000) CPUTYPE("Cortex-M4") FPU2 CLOCK(16000000) ELITTLE</Cpu>'
Add-Line '          <FlashUtilSpec></FlashUtilSpec>'
Add-Line '          <StartupFile></StartupFile>'
Add-Line '          <FlashDriverDll>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_512 -FS08000000 -FL080000 -FP0($$Device:STM32F407VETx$Flash\STM32F4xx_512.FLM))</FlashDriverDll>'
Add-Line '          <DeviceId>0</DeviceId>'
Add-Line '          <RegisterFile>$$Device:STM32F407VETx$Drivers\CMSIS\Device\ST\STM32F4xx\Include\stm32f4xx.h</RegisterFile>'
Add-Line '          <MemoryEnv></MemoryEnv>'
Add-Line '          <Cmp></Cmp>'
Add-Line '          <Asm></Asm>'
Add-Line '          <Linker></Linker>'
Add-Line '          <OHString></OHString>'
Add-Line '          <InfinionOptionDll></InfinionOptionDll>'
Add-Line '          <SLE66CMisc></SLE66CMisc>'
Add-Line '          <SLE66AMisc></SLE66AMisc>'
Add-Line '          <SLE66LinkerMisc></SLE66LinkerMisc>'
Add-Line '          <SFDFile>$$Device:STM32F407VETx$CMSIS\SVD\STM32F407.svd</SFDFile>'
Add-Line '          <bCustSvd>0</bCustSvd>'
Add-Line '          <UseEnv>0</UseEnv>'
Add-Line '          <BinPath></BinPath>'
Add-Line '          <IncludePath></IncludePath>'
Add-Line '          <LibPath></LibPath>'
Add-Line '          <RegisterFilePath></RegisterFilePath>'
Add-Line '          <DBRegisterFilePath></DBRegisterFilePath>'
Add-Line '          <TargetStatus>'
Add-Line '            <Error>0</Error>'
Add-Line '            <ExitCodeStop>0</ExitCodeStop>'
Add-Line '            <ButtonStop>0</ButtonStop>'
Add-Line '            <NotGenerated>0</NotGenerated>'
Add-Line '            <InvalidFlash>1</InvalidFlash>'
Add-Line '          </TargetStatus>'
Add-Line '          <OutputDirectory>..\..\build\keil\</OutputDirectory>'
Add-Line '          <OutputName>fw_f407</OutputName>'
Add-Line '          <CreateExecutable>1</CreateExecutable>'
Add-Line '          <CreateLib>0</CreateLib>'
Add-Line '          <CreateHexFile>1</CreateHexFile>'
Add-Line '          <DebugInformation>1</DebugInformation>'
Add-Line '          <BrowseInformation>1</BrowseInformation>'
Add-Line '          <ListingPath>..\..\build\keil\</ListingPath>'
Add-Line '          <HexOffset>0</HexOffset>'
Add-Line '          <HexSel>0</HexSel>'
Add-Line '          <AfterMake>'
Add-Line '            <RunUserProg1>0</RunUserProg1>'
Add-Line '            <RunUserProg2>0</RunUserProg2>'
Add-Line '            <UserProg1Name></UserProg1Name>'
Add-Line '            <UserProg2Name></UserProg2Name>'
Add-Line '            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>'
Add-Line '            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>'
Add-Line '            <nStopA1X>0</nStopA1X>'
Add-Line '            <nStopA2X>0</nStopA2X>'
Add-Line '          </AfterMake>'
Add-Line '          <SelectedForBatchBuild>0</SelectedForBatchBuild>'
Add-Line '          <SVCSIdString></SVCSIdString>'
Add-Line '        </TargetCommonOption>'
Add-Line '        <CommonProperty>'
Add-Line '          <UseCPPCompiler>0</UseCPPCompiler>'
Add-Line '          <RVCTCodeConst>0</RVCTCodeConst>'
Add-Line '          <RVCTZI>0</RVCTZI>'
Add-Line '          <RVCTOtherData>0</RVCTOtherData>'
Add-Line '          <ModuleSelection>0</ModuleSelection>'
Add-Line '          <IncludeInBuild>1</IncludeInBuild>'
Add-Line '          <AlwaysBuild>0</AlwaysBuild>'
Add-Line '          <GenerateAssemblyFile>0</GenerateAssemblyFile>'
Add-Line '          <AssembleAssemblyFile>0</AssembleAssemblyFile>'
Add-Line '          <PublicsOnly>0</PublicsOnly>'
Add-Line '          <StopOnExitCode>3</StopOnExitCode>'
Add-Line '          <CustomArgument></CustomArgument>'
Add-Line '          <IncludeLibraryModules></IncludeLibraryModules>'
Add-Line '          <ComprImg>1</ComprImg>'
Add-Line '        </CommonProperty>'
Add-Line '        <DllOption>'
Add-Line '          <SimDllName>SARMCM3.DLL</SimDllName>'
Add-Line '          <SimDllArguments> -REMAP -MPU</SimDllArguments>'
Add-Line '          <SimDlgDll>DCM.DLL</SimDlgDll>'
Add-Line '          <SimDlgDllArguments>-pCM4</SimDlgDllArguments>'
Add-Line '          <TargetDllName>SARMCM3.DLL</TargetDllName>'
Add-Line '          <TargetDllArguments> -MPU</TargetDllArguments>'
Add-Line '          <TargetDlgDll>TCM.DLL</TargetDlgDll>'
Add-Line '          <TargetDlgDllArguments>-pCM4</TargetDlgDllArguments>'
Add-Line '        </DllOption>'
Add-Line '        <DebugOption>'
Add-Line '          <OPTHX>'
Add-Line '            <HexSelection>1</HexSelection>'
Add-Line '            <HexRangeLowAddress>0</HexRangeLowAddress>'
Add-Line '            <HexRangeHighAddress>0</HexRangeHighAddress>'
Add-Line '            <HexOffset>0</HexOffset>'
Add-Line '            <Oh166RecLen>16</Oh166RecLen>'
Add-Line '          </OPTHX>'
Add-Line '        </DebugOption>'
Add-Line '        <Utilities>'
Add-Line '          <Flash1>'
Add-Line '            <UseTargetDll>1</UseTargetDll>'
Add-Line '            <UseExternalTool>0</UseExternalTool>'
Add-Line '            <RunIndependent>0</RunIndependent>'
Add-Line '            <UpdateFlashBeforeDebugging>1</UpdateFlashBeforeDebugging>'
Add-Line '            <Capability>1</Capability>'
Add-Line '            <DriverSelection>4097</DriverSelection>'
Add-Line '          </Flash1>'
Add-Line '          <bUseTDR>1</bUseTDR>'
Add-Line '          <Flash2>BIN\CMSIS_AGDI.dll</Flash2>'
Add-Line '          <Flash3></Flash3>'
Add-Line '          <Flash4></Flash4>'
Add-Line '          <pFcarmOut></pFcarmOut>'
Add-Line '          <pFcarmGrp></pFcarmGrp>'
Add-Line '          <pFcArmRoot></pFcArmRoot>'
Add-Line '          <FcArmLst>0</FcArmLst>'
Add-Line '        </Utilities>'
Add-Line '        <TargetArmAds>'
Add-Line '          <ArmAdsMisc>'
Add-Line '            <GenerateListings>0</GenerateListings>'
Add-Line '            <asHll>1</asHll>'
Add-Line '            <asAsm>1</asAsm>'
Add-Line '            <asMacX>1</asMacX>'
Add-Line '            <asSyms>1</asSyms>'
Add-Line '            <asFals>1</asFals>'
Add-Line '            <asDbgD>1</asDbgD>'
Add-Line '            <asForm>1</asForm>'
Add-Line '            <ldLst>0</ldLst>'
Add-Line '            <ldmm>1</ldmm>'
Add-Line '            <ldXref>1</ldXref>'
Add-Line '            <BigEnd>0</BigEnd>'
Add-Line '            <AdsALst>1</AdsALst>'
Add-Line '            <AdsACrf>1</AdsACrf>'
Add-Line '            <AdsANop>0</AdsANop>'
Add-Line '            <AdsANot>0</AdsANot>'
Add-Line '            <AdsLLst>1</AdsLLst>'
Add-Line '            <AdsLmap>1</AdsLmap>'
Add-Line '            <AdsLcgr>1</AdsLcgr>'
Add-Line '            <AdsLsym>1</AdsLsym>'
Add-Line '            <AdsLszi>1</AdsLszi>'
Add-Line '            <AdsLtoi>1</AdsLtoi>'
Add-Line '            <AdsLsun>1</AdsLsun>'
Add-Line '            <AdsLven>1</AdsLven>'
Add-Line '            <AdsLsxf>1</AdsLsxf>'
Add-Line '            <RvctClst>0</RvctClst>'
Add-Line '            <GenPPlst>0</GenPPlst>'
Add-Line '            <AdsCpuType>"Cortex-M4"</AdsCpuType>'
Add-Line '            <RvctDeviceName></RvctDeviceName>'
Add-Line '            <mOS>0</mOS>'
Add-Line '            <uocRom>0</uocRom>'
Add-Line '            <uocRam>0</uocRam>'
Add-Line '            <hadIROM>1</hadIROM>'
Add-Line '            <hadIRAM>1</hadIRAM>'
Add-Line '            <hadXRAM>0</hadXRAM>'
Add-Line '            <uocXRam>0</uocXRam>'
Add-Line '            <RvdsVP>2</RvdsVP>'
Add-Line '            <RvdsMve>0</RvdsMve>'
Add-Line '            <RvdsCdeCp>0</RvdsCdeCp>'
Add-Line '            <nBranchProt>0</nBranchProt>'
Add-Line '            <hadIRAM2>1</hadIRAM2>'
Add-Line '            <hadIROM2>0</hadIROM2>'
Add-Line '            <StupSel>8</StupSel>'
Add-Line '            <useUlib>0</useUlib>'
Add-Line '            <EndSel>0</EndSel>'
Add-Line '            <uLtcg>0</uLtcg>'
Add-Line '            <nSecure>0</nSecure>'
Add-Line '            <RoSelD>3</RoSelD>'
Add-Line '            <RwSelD>3</RwSelD>'
Add-Line '            <CodeSel>0</CodeSel>'
Add-Line '            <OptFeed>0</OptFeed>'
Add-Line '            <NoZi1>0</NoZi1>'
Add-Line '            <NoZi2>0</NoZi2>'
Add-Line '            <NoZi3>0</NoZi3>'
Add-Line '            <NoZi4>0</NoZi4>'
Add-Line '            <NoZi5>0</NoZi5>'
Add-Line '            <Ro1Chk>0</Ro1Chk>'
Add-Line '            <Ro2Chk>0</Ro2Chk>'
Add-Line '            <Ro3Chk>0</Ro3Chk>'
Add-Line '            <Ir1Chk>1</Ir1Chk>'
Add-Line '            <Ir2Chk>0</Ir2Chk>'
Add-Line '            <Ra1Chk>0</Ra1Chk>'
Add-Line '            <Ra2Chk>0</Ra2Chk>'
Add-Line '            <Ra3Chk>0</Ra3Chk>'
Add-Line '            <Im1Chk>1</Im1Chk>'
Add-Line '            <Im2Chk>0</Im2Chk>'
Add-Line '            <OnChipMemories>'
foreach ($mem in @('Ocm1','Ocm2','Ocm3','Ocm4','Ocm5','Ocm6')) {
    Add-Line "              <$mem>"
    Add-Line '                <Type>0</Type>'
    Add-Line '                <StartAddress>0x0</StartAddress>'
    Add-Line '                <Size>0x0</Size>'
    Add-Line "              </$mem>"
}
Add-Line '              <IRAM>'
Add-Line '                <Type>0</Type>'
Add-Line '                <StartAddress>0x20000000</StartAddress>'
Add-Line '                <Size>0x20000</Size>'
Add-Line '              </IRAM>'
Add-Line '              <IROM>'
Add-Line '                <Type>1</Type>'
Add-Line '                <StartAddress>0x8000000</StartAddress>'
Add-Line '                <Size>0x80000</Size>'
Add-Line '              </IROM>'
Add-Line '              <XRAM>'
Add-Line '                <Type>0</Type>'
Add-Line '                <StartAddress>0x0</StartAddress>'
Add-Line '                <Size>0x0</Size>'
Add-Line '              </XRAM>'
for ($i = 1; $i -le 10; $i++) {
    Add-Line "              <OCR_RVCT$i>"
    Add-Line '                <Type>0</Type>'
    Add-Line '                <StartAddress>0x0</StartAddress>'
    Add-Line '                <Size>0x0</Size>'
    Add-Line "              </OCR_RVCT$i>"
}
Add-Line '            </OnChipMemories>'
Add-Line '            <RvctStartVector></RvctStartVector>'
Add-Line '          </ArmAdsMisc>'
Add-Line '          <Cads>'
Add-Line '            <interw>1</interw>'
Add-Line '            <Optim>1</Optim>'
Add-Line '            <oTime>0</oTime>'
Add-Line '            <SplitLS>0</SplitLS>'
Add-Line '            <OneElfS>1</OneElfS>'
Add-Line '            <Strict>0</Strict>'
Add-Line '            <EnumInt>0</EnumInt>'
Add-Line '            <PlainCh>0</PlainCh>'
Add-Line '            <Ropi>0</Ropi>'
Add-Line '            <Rwpi>0</Rwpi>'
Add-Line '            <wLevel>2</wLevel>'
Add-Line '            <uThumb>0</uThumb>'
Add-Line '            <uSurpInc>0</uSurpInc>'
Add-Line '            <uC99>1</uC99>'
Add-Line '            <uGnu>0</uGnu>'
Add-Line '            <useXO>0</useXO>'
Add-Line '            <v6Lang>0</v6Lang>'
Add-Line '            <v6LangP>0</v6LangP>'
Add-Line '            <vShortEn>1</vShortEn>'
Add-Line '            <vShortWch>1</vShortWch>'
Add-Line '            <v6Lto>0</v6Lto>'
Add-Line '            <v6WtE>0</v6WtE>'
Add-Line '            <v6Rtti>0</v6Rtti>'
Add-Line '            <VariousControls>'
Add-Line '              <MiscControls></MiscControls>'
Add-Line "              <Define>$(ConvertTo-XmlText $defineAttr)</Define>"
Add-Line '              <Undefine></Undefine>'
Add-Line "              <IncludePath>$(ConvertTo-XmlText $includeAttr)</IncludePath>"
Add-Line '            </VariousControls>'
Add-Line '          </Cads>'
Add-Line '          <Aads>'
Add-Line '            <interw>1</interw>'
Add-Line '            <Ropi>0</Ropi>'
Add-Line '            <Rwpi>0</Rwpi>'
Add-Line '            <thumb>0</thumb>'
Add-Line '            <SplitLS>0</SplitLS>'
Add-Line '            <SwStkChk>0</SwStkChk>'
Add-Line '            <NoWarn>0</NoWarn>'
Add-Line '            <uSurpInc>0</uSurpInc>'
Add-Line '            <useXO>0</useXO>'
Add-Line '            <ClangAsOpt>4</ClangAsOpt>'
Add-Line '            <VariousControls>'
Add-Line '              <MiscControls></MiscControls>'
Add-Line '              <Define></Define>'
Add-Line '              <Undefine></Undefine>'
Add-Line '              <IncludePath></IncludePath>'
Add-Line '            </VariousControls>'
Add-Line '          </Aads>'
Add-Line '          <LDads>'
Add-Line '            <umfTarg>0</umfTarg>'
Add-Line '            <Ropi>0</Ropi>'
Add-Line '            <Rwpi>0</Rwpi>'
Add-Line '            <noStLib>0</noStLib>'
Add-Line '            <RepFail>1</RepFail>'
Add-Line '            <useFile>1</useFile>'
Add-Line '            <TextAddressRange>0x08000000</TextAddressRange>'
Add-Line '            <DataAddressRange>0x20000000</DataAddressRange>'
Add-Line '            <pXoBase></pXoBase>'
Add-Line "            <ScatterFile>$(ConvertTo-XmlText $scatterAttr)</ScatterFile>"
Add-Line '            <IncludeLibs></IncludeLibs>'
Add-Line '            <IncludeLibsPath></IncludeLibsPath>'
Add-Line '            <Misc></Misc>'
Add-Line '            <LinkerInputFile></LinkerInputFile>'
Add-Line '            <DisabledWarnings></DisabledWarnings>'
Add-Line '          </LDads>'
Add-Line '        </TargetArmAds>'
Add-Line '      </TargetOption>'
Add-Line '      <Groups>'
foreach ($name in $groups.Keys) {
    Add-Line '        <Group>'
    Add-Line "          <GroupName>$name</GroupName>"
    Add-Line '          <Files>'
    foreach ($rel in $groups[$name]) {
        $fileName = [System.IO.Path]::GetFileName($rel)
        $fileType = Get-FileTypeCode $rel
        $filePath = ConvertTo-XmlText (Get-RelativeToProject $rel)
        Add-Line '            <File>'
        Add-Line "              <FileName>$fileName</FileName>"
        Add-Line "              <FileType>$fileType</FileType>"
        Add-Line "              <FilePath>$filePath</FilePath>"
        Add-Line '            </File>'
    }
    Add-Line '          </Files>'
    Add-Line '        </Group>'
}
Add-Line '      </Groups>'
Add-Line '    </Target>'
Add-Line '  </Targets>'
Add-Line ''
Add-Line '</Project>'

# Write UTF-8 without a BOM: uVision is happy either way, but a BOM makes the
# file show up as modified in every diff tool and some XML readers choke on it.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutputPath, $sb.ToString(), $utf8NoBom)
Write-Host "wrote $OutputPath" -ForegroundColor Green
Write-Host ("  target : {0}  (ARM Compiler 5 / armcc)" -f $Configuration)
Write-Host ("  groups : {0}" -f (($groups.Keys) -join ', '))
Write-Host ("  sources: {0} files" -f $grouped.Count)
Write-Host ("  defines: {0}" -f $defineAttr)

try {
    $null = [xml](Get-Content -Raw -Encoding UTF8 $OutputPath)
    Write-Host '  XML    : well-formed' -ForegroundColor Green
} catch {
    throw "generated XML is not well-formed: $($_.Exception.Message)"
}

if ($Build) {
    $uv4 = @(
        'D:\keil5\stm32\UV4\UV4.exe'
        (Join-Path ${env:ProgramFiles(x86)} 'Keil_v5\UV4\UV4.exe')
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $uv4) { throw 'UV4.exe not found; run without -Build or add UV4 to PATH' }

    $log = Join-Path $repoRoot 'build\keil\build.log'
    New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
    if (Test-Path $log) { Remove-Item $log -Force }

    Write-Host "building with $uv4 ..." -ForegroundColor Cyan
    # No -j switch: '-j0' means "zero parallel jobs" and makes uVision report a
    # successful build while compiling nothing.
    $uvArgs = @('-r', $OutputPath, '-o', $log)
    $proc = Start-Process -FilePath $uv4 -ArgumentList $uvArgs -Wait -PassThru -NoNewWindow
    Write-Host "UV4 exit code: $($proc.ExitCode)"
    if (Test-Path $log) { Get-Content $log }
    if ($proc.ExitCode -ne 0) { throw "uVision build failed with exit code $($proc.ExitCode)" }
    Write-Host 'build OK' -ForegroundColor Green
}
