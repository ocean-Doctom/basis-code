# Basis Code plugin marketplace

The `marketplace` branch of [ocean-Doctom/basis-code](https://github.com/ocean-Doctom/basis-code/tree/marketplace)
serves `catalog.json` and `packages/*.zip`. Basis Code 1.7.0 loads this catalog,
downloads a selected package over HTTPS, verifies its SHA-256 and manifest,
extracts regular files into a temporary directory, then activates the package.
Fresh installations include only the marketplace. Other entries are optional.

These are Basis Code native-engine packages, not VSIX or arbitrary executable
plugins. The host contains the native engines; packages activate the selected
engine and distribute its matching source and licenses. Installing a package
does not download compilers or execute a post-install script.

## Rebuilding

Run `python marketplace/build-packages.py` from a product checkout. The script
creates deterministic stored ZIPs and writes their sizes and SHA-256 values into
the catalog. Publish the resulting catalog at the branch root and the ZIPs under
`packages/`. Keep versioned package URLs immutable once released.

Format `basis-extension-v1` supports at most 64 flat regular files, 2 MiB per file,
4 MiB in total. Compressed, encrypted, linked, duplicate, nested, traversal and
Windows reserved filenames are rejected. The manifest must match the catalog's
ID, engine, name and version. Unknown engines cannot be loaded by this host.

Legacy auto-created plugin directories are moved to hidden `.legacy-*` backups
before first package installation. Uninstalling AI Agent preserves the separate
encrypted `accounts/ai-agent` credentials. Existing ChatGPT OAuth credentials
remain in their original directory. DeepSeek keys are migrated once on opening
AI Agent settings; no key is included in these packages.

## AI compatibility references

AI Agent implements OpenAI-compatible Chat Completions, streaming replies,
manual or service-provided model lists, and function calling for Agent mode.
Provider-specific advanced features are not implied. Official compatibility docs:

- [DeepSeek](https://api-docs.deepseek.com/quick_start/agent_integrations/)
- [Gemini](https://ai.google.dev/gemini-api/docs/openai)
- [Claude](https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk)
- [Qwen / Model Studio](https://help.aliyun.com/en/model-studio/compatibility-of-openai-with-dashscope)
- [LLMQore SSE parser source and MIT license](https://github.com/Palm1r/llmqore)

Cloud keys and paid inference are never used by the regression suite. Injected Qt reply
fixtures verify request routing, model selection, Chat versus Agent payloads and
response parsing. Live provider authentication and model access require user testing.
