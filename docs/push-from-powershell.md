# Push the implementation to your own GitHub repository

The current branch is `implement-pi-logstar`. Its changes must be committed before
pushing. The existing `origin` is `https://github.com/ladnir/secure-join.git`.
The commands below create a new private repository in the GitHub account you
authenticate, add it as remote `personal`, and push the implementation branch.

Install GitHub CLI if `gh` is not recognized:

```powershell
winget install --id GitHub.cli --exact
```

After installation, reopen PowerShell, then run:

```powershell
Set-Location "C:\Users\stani\OneDrive\Documents\LOGSTAR\secure-join"
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git
git switch implement-pi-logstar
git add .gitignore CMakeLists.txt README.md frontend secure-join tests scripts docs
git commit -m "Implement two-party pi-logstar and reproducible evaluation"
gh repo create logstar-implementation --private --source . --remote personal
git push --set-upstream personal implement-pi-logstar
```

The repository will be named `YOUR-GITHUB-ACCOUNT/logstar-implementation`.
The paper is a separate local Git repository; these commands push the code and
its benchmark artifacts. For subsequent updates after committing, use:

```powershell
git push personal implement-pi-logstar
```

If Git reports dubious ownership for this particular directory, trust only this
known checkout and rerun the failed Git command:

```powershell
git config --global --add safe.directory "C:/Users/stani/OneDrive/Documents/LOGSTAR/secure-join"
```

Command references: [creating a repository](https://cli.github.com/manual/gh_repo_create)
and [browser authentication](https://cli.github.com/manual/gh_auth_login).
