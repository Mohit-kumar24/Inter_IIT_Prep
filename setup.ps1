[CmdletBinding()]
param(
    [ValidateSet("Setup", "Build", "Generate", "PartD", "Export", "All")]
    [string]$Action = "Setup",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigPath = Join-Path $ProjectRoot ".env"
$ConfigTemplate = Join-Path $ProjectRoot ".env.example"
$EnvironmentRoot = Join-Path $ProjectRoot "env"
$VenvRoot = Join-Path $EnvironmentRoot ".venv"
$PythonPath = Join-Path $VenvRoot "Scripts\python.exe"

function Read-ProjectConfig {
    if (-not (Test-Path -LiteralPath $ConfigPath)) {
        Copy-Item -LiteralPath $ConfigTemplate -Destination $ConfigPath
        Write-Host "Created local configuration: .env"
    }

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $ConfigPath) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }

        $parts = $trimmed -split "=", 2
        if ($parts.Count -ne 2 -or -not $parts[0].Trim()) {
            throw "Invalid .env entry: $line"
        }
        $values[$parts[0].Trim()] = $parts[1].Trim()
    }
    return $values
}

function Get-ConfigValue {
    param(
        [hashtable]$Config,
        [string]$Name,
        [string]$Default
    )
    if ($Config.ContainsKey($Name)) {
        return [string]$Config[$Name]
    }
    return $Default
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Invoke-Setup {
    if (-not (Test-Path -LiteralPath $PythonPath)) {
        $bootstrapName = Get-ConfigValue -Config $Config -Name "FJSP_BOOTSTRAP_PYTHON" -Default "python"
        $bootstrap = Get-Command $bootstrapName -ErrorAction SilentlyContinue
        if (-not $bootstrap) {
            throw "Python command '$bootstrapName' was not found. Install Python 3.10+ or update FJSP_BOOTSTRAP_PYTHON in .env."
        }
        New-Item -ItemType Directory -Path $EnvironmentRoot -Force | Out-Null
        Invoke-Checked -FilePath $bootstrap.Source -Arguments @("-m", "venv", $VenvRoot)
    }

    if (-not $SkipInstall) {
        Invoke-Checked -FilePath $PythonPath -Arguments @("-m", "pip", "install", "-r", (Join-Path $ProjectRoot "requirements.txt"))
    }
    Write-Host "Python environment ready: $VenvRoot"
}

function Invoke-Build {
    $compilerName = Get-ConfigValue -Config $Config -Name "FJSP_CXX" -Default "g++"
    $compilerCommand = Get-Command $compilerName -ErrorAction SilentlyContinue
    if (-not $compilerCommand) {
        throw "C++ compiler '$compilerName' was not found. Install a C++17 compiler or update FJSP_CXX in .env."
    }
    $flagsText = Get-ConfigValue -Config $Config -Name "FJSP_BUILD_FLAGS" -Default "-O3 -std=c++17"
    $flags = @($flagsText -split "\s+" | Where-Object { $_ })
    $targets = @(
        @{ Source = "generator/gen_core.cpp"; Output = "generator/gen_core.exe" },
        @{ Source = "algorithms/greedy_spt.cpp"; Output = "algorithms/greedy_spt.exe" },
        @{ Source = "algorithms/local_search.cpp"; Output = "algorithms/local_search.exe" },
        @{ Source = "algorithms/simulated_annealing.cpp"; Output = "algorithms/simulated_annealing.exe" },
        @{ Source = "algorithms/tabu_search.cpp"; Output = "algorithms/tabu_search.exe" },
        @{ Source = "algorithms/genetic_algorithm.cpp"; Output = "algorithms/genetic_algorithm.exe" },
        @{ Source = "algorithms/memetic_algorithm.cpp"; Output = "algorithms/memetic_algorithm.exe" },
        @{ Source = "environment/validator.cpp"; Output = "environment/validator.exe" },
        @{ Source = "environment/evaluator.cpp"; Output = "environment/evaluator.exe" }
    )

    foreach ($target in $targets) {
        $source = Join-Path $ProjectRoot $target.Source
        $output = Join-Path $ProjectRoot $target.Output
        Write-Host "Building $($target.Output)"
        $arguments = @($flags) + @($source, "-o", $output)
        Invoke-Checked -FilePath $compilerCommand.Source -Arguments $arguments
    }
}

function Invoke-Generate {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "generator/gen_core.exe"))) {
        Invoke-Build
    }
    $jobs = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_JOBS" -Default "10"
    $machines = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_MACHINES" -Default "5"
    $seeds = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_SEEDS" -Default "1,2,3"
    $output = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_OUTPUT" -Default "instances/generated_batch"
    $script = Join-Path $ProjectRoot "generator/generate_instances.py"
    $arguments = @($script, "batch", "--jobs", $jobs, "--machines", $machines, "--seeds", $seeds, "--output-dir", $output)
    $classes = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_CLASSES" -Default ""
    if ($classes) {
        $arguments += @("--classes", $classes)
    }
    $plots = Get-ConfigValue -Config $Config -Name "FJSP_GENERATE_PLOTS" -Default "false"
    if ($plots -match "^(1|true|yes)$") {
        $arguments += "--plot"
    }
    Invoke-Checked -FilePath $PythonPath -Arguments $arguments
}

function Invoke-PartD {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "environment/validator.exe"))) {
        Invoke-Build
    }
    $timeout = Get-ConfigValue -Config $Config -Name "FJSP_PART_D_TIMEOUT" -Default "180"
    Invoke-Checked -FilePath $PythonPath -Arguments @(
        (Join-Path $ProjectRoot "environment/run_part_d_experiments.py"),
        "--timeout", $timeout
    )
}

function Invoke-Export {
    Invoke-Checked -FilePath $PythonPath -Arguments @(
        (Join-Path $ProjectRoot "environment/export_report_tables.py")
    )
}

Push-Location $ProjectRoot
try {
    $Config = Read-ProjectConfig
    switch ($Action) {
        "Setup" {
            Invoke-Setup
        }
        "Build" {
            Invoke-Build
        }
        "Generate" {
            if (-not (Test-Path -LiteralPath $PythonPath)) {
                Invoke-Setup
            }
            Invoke-Generate
        }
        "PartD" {
            if (-not (Test-Path -LiteralPath $PythonPath)) {
                Invoke-Setup
            }
            Invoke-Build
            Invoke-PartD
        }
        "Export" {
            if (-not (Test-Path -LiteralPath $PythonPath)) {
                Invoke-Setup
            }
            Invoke-Export
        }
        "All" {
            Invoke-Setup
            Invoke-Build
            Invoke-PartD
            Invoke-Export
        }
    }
}
finally {
    Pop-Location
}
