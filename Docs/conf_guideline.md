📄 **YAML Configuration Usage Guide**

---

### 🔧 **Purpose**
The YAML configuration file enables **fastrpc** to set machine-specific configurations at runtime. Each machine entry corresponds to a specific hardware platform.

- fastrpc supports reading YAML configuration files from a particular directory. Users should ensure all configuration files are stored in that same directory.
  - For Linux platforms: `/usr/share/qcom/conf.d/`
- In case of multiple configuration files defining path for a single machine, the directory is parsed in lexicographical order and the latest one carrying the
  machine path is picked.
- **Machine Name**: Obtain the machine name for your platform from:
  ```
  /sys/firmware/devicetree/base/model
  ```
  (fastrpc uses same path for matching machine names)
---
### 📄 **Current Properties**
- **DSP_LIBRARY_PATH**: Specifies the path to DSP binaries and resources for the Machine. Path is relative to `/usr/share/qcom/`.
- **ADSP_ARCH / MDSP_ARCH / SDSP_ARCH / CDSP_ARCH** *(optional)*:
  Specifies the Hexagon architecture version for the corresponding DSP domain. Used as a fallback on
  legacy targets where the `ARCH_VER` kernel capability is not supported. Value must match `v[0-9a-f]+`
  (e.g. `v60`, `v65`, `v66`). Only the domain(s) present on the target need to be specified.
---

### 📁 **Format Guidelines**
Each machine has its own `.yaml` file under the configuration directory:
```
machines:
  Machine Name:
    DSP_LIBRARY_PATH: relative/path/to/dsp/binaries
    CDSP_ARCH: v60   # optional, for legacy targets
```

**Key Points:**
- The root element is `machines:`
- Machine name is an unquoted key under `machines:`
- Properties are indented under each machine name
- Use proper YAML indentation
- `DSP_LIBRARY_PATH` is a relative path (relative to `/usr/share/qcom/`), unquoted, no leading slash
- Avoid tabs (use spaces only)

---

### ✅ **Example Configuration**
```
machines:
  Qualcomm Technologies, Inc. DB820c:
    DSP_LIBRARY_PATH: apq8096/Qualcomm/db820c/dsp
    ADSP_ARCH: v60
```


---

### ⚠️ **Important Notes**
- Do **not** modify machine names unless adding a new supported Machine.
- Ensure `DSP_LIBRARY_PATH` values:
  - Are **relative to `/usr/share/qcom/`** with no leading slash.
  - Are unquoted.
- Ensure `*_ARCH` values match the pattern `v[0-9a-f]+` (e.g. `v60`, `v65`, `v66`).
- Follow YAML syntax rules:
  - Use consistent indentation.
  - Ensure proper spacing after colons (`: `).
  - Avoid tabs (use spaces only).
- Maintain:
  - Proper YAML structure and hierarchy.
  - Consistent formatting across entries.
- When adding new properties:
  - Document their purpose **here**.
  - Follow the same indentation pattern.
- Do **not** create duplicate Machine entries.
- Validate YAML syntax before deployment to avoid parsing errors.

---

### ➕ **Adding New Platforms**
Create a new file under the configuration directory:
```
machines:
  New Machine Name:
    DSP_LIBRARY_PATH: new_machine/path/dsp
    ADSP_ARCH: v<XY>   # optional, for legacy targets
    CDSP_ARCH: v<XY>   # optional, for legacy targets
```

Ensure the new entry is properly indented under the `machines:` root element and follows YAML syntax conventions.

---

### 📝 **File Naming**
Configuration files should use the `.yaml` or `.yml` extension and be placed in the designated configuration directory (`/usr/share/qcom/conf.d/` on Linux platforms).

### ✅ Schema Validation
To ensure the configuration file adheres to the required structure, validate it against the schema provided.

Schema File Location:
<ROOT>/Docs/schemas/fastrpc-config-schema.yaml

Validation Command:
Use Yamale for schema validation:
yamale -s fastrpc-config-schema.yaml <yaml file>
