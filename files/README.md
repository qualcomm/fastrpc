
## 📄 `machine_dsp_paths.ini` Usage Guide

This folder contains the `machine_dsp_paths.ini` file, which defines DSP path mappings for various Qualcomm-based platforms. It is **critical** that the format of this `.ini` file is preserved exactly as shown to ensure proper parsing and functionality.

### 🔧 Purpose

The `machine_dsp_paths.ini` file provides platform-specific paths used by automation scripts and system tools to locate DSP binaries and resources. Each section corresponds to a specific hardware platform.

### 📁 Format Guidelines

Each entry in the `.ini` file must follow this structure:

```ini
[Platform Name]
path = "<absolute path to DSP binaries and resources for the platform>"
```

#### ✅ Example Entries

```ini
[Qualcomm Technologies, Inc. DB820c]
path = "/usr/share/qcom/apq8096/Qualcomm/db820c/"

[Thundercomm Dragonboard 845c]
path = "/usr/share/qcom/sdm845/Thundercomm/db845c/"
```

### ⚠️ Important Notes

- **Do not modify section headers** (`[Platform Name]`) unless adding a new supported platform.
- **Ensure paths are enclosed in double quotes** (`"..."`) and are absolute.
- **Avoid trailing spaces or special characters** that may break parsing.
- **Maintain consistent indentation and line breaks**.
- **Add new property under same platform name, don't create new duplicate platfomr entries**.
- **When adding new properties document their purpose in Readme file**.

### ➕ Adding New Platforms

To add a new platform, follow the existing format:

```ini
[New Platform Name]
path = "/usr/share/qcom/new_platform/path/"
```

### 📌 Usage

This `.ini` file is consumed by internal tools and scripts that rely on these mappings to locate DSP binaries. Any deviation from the expected format may result in failures during build, deployment, or runtime.
