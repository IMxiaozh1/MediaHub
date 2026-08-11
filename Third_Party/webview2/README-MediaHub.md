# Microsoft WebView2 SDK 版本记录

## 包与来源

- NuGet 包：`Microsoft.Web.WebView2`
- 版本：`1.0.4129.50`
- 包文件：`microsoft.web.webview2.1.0.4129.50.nupkg`
- 包大小：`9,245,553` 字节
- NuGet 元数据作者：`Microsoft`
- NuGet 官方全局源：
  `https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4129.50/microsoft.web.webview2.1.0.4129.50.nupkg`
- 实际取得方式：用户通过 Azure China NuGet 镜像下载：
  `https://nuget.azure.cn/v3-flatcontainer/microsoft.web.webview2/1.0.4129.50/microsoft.web.webview2.1.0.4129.50.nupkg`
- SHA-256：
  `D3934F482D484B89FB4825DF720C710664E1143A1E90F7B3A60794EF33F473D2`

## 签名核验

NuGet Author 签名和 Repository 副署核验退出码为 `0`。核验输出识别到：

- Author 签名主体：`Microsoft Corporation`
- Author 签名证书 SHA-256：
  `566A31882BE208BE4422F7CFD66ED09F5D4524A5994F50CCC8B05EC0528C1353`
- Repository 副署主体：`NuGet.org Repository by Microsoft`
- Repository 副署证书 SHA-256：
  `1F4B311D9ACC115C8DC8018B5A49E00FCE6DA8E2855F9F014CA6F34570BC482D`

核验环境离线，无法访问证书撤销服务器，因此命令输出了撤销检查警告。签名核验
本身以退出码 `0` 完成，但证书撤销状态未知；本记录不声称证书处于未吊销状态。

## 纳入范围

以下文件从 NuGet 包对应条目逐字节复制，未作修改：

- `include/WebView2.h`
- `include/WebView2EnvironmentOptions.h`
- `x64/WebView2LoaderStatic.lib`
- `LICENSE.txt`
- `NOTICE.txt`
- `Microsoft.Web.WebView2.nuspec`

仓库不纳入 x86 或 ARM64 架构文件、WebView2 DLL、Runtime、.NET 程序集、工具和
用户资料。Microsoft Edge WebView2 Evergreen Runtime 是独立安装和更新的外部运行时，
不进入本仓库；此依赖阶段也不把 Runtime 复制到发布目录。

## 复核命令

以下命令重新核对包大小、包哈希和 NuGet 签名：

```powershell
$packagePath = "<下载目录>\microsoft.web.webview2.1.0.4129.50.nupkg"
Get-Item -LiteralPath $packagePath | Select-Object FullName, Length
Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath
dotnet nuget verify $packagePath --all
```

以下命令把包解压到临时复核目录，并比较六个仓库文件与包内对应条目的 SHA-256：

```powershell
$reviewRoot = Join-Path $env:TEMP "mediahub-webview2-1.0.4129.50-review"
$archiveCopy = Join-Path $env:TEMP "mediahub-webview2-1.0.4129.50-review.zip"
Copy-Item -LiteralPath $packagePath -Destination $archiveCopy -Force
Expand-Archive -LiteralPath $archiveCopy -DestinationPath $reviewRoot -Force

$files = @(
    @("build\native\include\WebView2.h", "Third_Party\webview2\include\WebView2.h"),
    @("build\native\include\WebView2EnvironmentOptions.h", "Third_Party\webview2\include\WebView2EnvironmentOptions.h"),
    @("build\native\x64\WebView2LoaderStatic.lib", "Third_Party\webview2\x64\WebView2LoaderStatic.lib"),
    @("LICENSE.txt", "Third_Party\webview2\LICENSE.txt"),
    @("NOTICE.txt", "Third_Party\webview2\NOTICE.txt"),
    @("Microsoft.Web.WebView2.nuspec", "Third_Party\webview2\Microsoft.Web.WebView2.nuspec")
)

$files | ForEach-Object {
    $packageFile = Join-Path $reviewRoot $_[0]
    $packageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $packageFile).Hash
    $repositoryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_[1]).Hash
    [PSCustomObject]@{
        File = $_[1]
        PackageHash = $packageHash
        RepositoryHash = $repositoryHash
        IsEqual = $packageHash -eq $repositoryHash
    }
}
```
