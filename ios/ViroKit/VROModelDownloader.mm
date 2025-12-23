//
//  VROModelDownloader.mm
//  ViroKit
//
//  Copyright © 2024 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#import "VROModelDownloader.h"
#import <compression.h>

// Error domain for model download errors
static NSString * const VROModelDownloaderErrorDomain = @"com.viro.modeldownloader";

// Forward declaration of delegate class
@class VROModelDownloadDelegate;

#pragma mark - Download Delegate

@interface VROModelDownloadDelegate : NSObject <NSURLSessionDownloadDelegate>
@property (nonatomic, copy) NSString *modelName;
@property (nonatomic, copy) VROModelDownloadProgress progressBlock;
@property (nonatomic, copy) VROModelDownloadCompletion completionBlock;
- (instancetype)initWithModelName:(NSString *)modelName
                         progress:(VROModelDownloadProgress)progressBlock
                       completion:(VROModelDownloadCompletion)completionBlock;
@end

// Error codes
typedef NS_ENUM(NSInteger, VROModelDownloaderError) {
    VROModelDownloaderErrorDownloadFailed = 1000,
    VROModelDownloaderErrorExtractionFailed = 1001,
    VROModelDownloaderErrorInvalidResponse = 1002,
    VROModelDownloaderErrorFileSystemError = 1003,
};

@implementation VROModelDownloader

#pragma mark - Model Status

+ (BOOL)isModelDownloaded:(NSString *)modelName {
    NSString *modelPath = [self localPathForModel:modelName];
    BOOL isDirectory = NO;
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:modelPath isDirectory:&isDirectory];
    return exists && isDirectory;
}

+ (NSString *)localPathForModel:(NSString *)modelName {
    NSString *modelsDir = [self modelsDirectory];
    return [modelsDir stringByAppendingPathComponent:
            [NSString stringWithFormat:@"%@.mlmodelc", modelName]];
}

+ (unsigned long long)modelSizeOnDisk:(NSString *)modelName {
    NSString *modelPath = [self localPathForModel:modelName];
    return [self sizeOfDirectoryAtPath:modelPath];
}

+ (unsigned long long)sizeOfDirectoryAtPath:(NSString *)path {
    NSFileManager *fileManager = [NSFileManager defaultManager];
    BOOL isDirectory = NO;

    if (![fileManager fileExistsAtPath:path isDirectory:&isDirectory]) {
        return 0;
    }

    if (!isDirectory) {
        NSDictionary *attrs = [fileManager attributesOfItemAtPath:path error:nil];
        return [attrs fileSize];
    }

    unsigned long long totalSize = 0;
    NSDirectoryEnumerator *enumerator = [fileManager enumeratorAtPath:path];
    NSString *file;

    while ((file = [enumerator nextObject])) {
        NSString *fullPath = [path stringByAppendingPathComponent:file];
        NSDictionary *attrs = [fileManager attributesOfItemAtPath:fullPath error:nil];
        if (attrs[NSFileType] == NSFileTypeRegular) {
            totalSize += [attrs fileSize];
        }
    }

    return totalSize;
}

#pragma mark - Download

+ (void)downloadModelIfNeeded:(NSString *)modelName
                      fromURL:(NSURL *)baseURL
                     progress:(VROModelDownloadProgress)progressBlock
                   completion:(VROModelDownloadCompletion)completion {

    // Check if already downloaded
    if ([self isModelDownloaded:modelName]) {
        NSString *localPath = [self localPathForModel:modelName];
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(localPath, nil);
        });
        return;
    }

    // Download the model
    [self downloadModel:modelName fromURL:baseURL progress:progressBlock completion:completion];
}

+ (void)downloadModel:(NSString *)modelName
              fromURL:(NSURL *)baseURL
             progress:(VROModelDownloadProgress)progressBlock
           completion:(VROModelDownloadCompletion)completion {

    // Construct the full URL for the model zip file
    NSString *zipFileName = [NSString stringWithFormat:@"%@.mlmodelc.zip", modelName];
    NSURL *downloadURL = [baseURL URLByAppendingPathComponent:zipFileName];

    // Create a download task with progress tracking
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
    config.timeoutIntervalForResource = 300; // 5 minute timeout for large models

    // Create a delegate to track progress
    VROModelDownloadDelegate *delegate = [[VROModelDownloadDelegate alloc] initWithModelName:modelName
                                                                                   progress:progressBlock
                                                                                 completion:completion];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:config
                                                          delegate:delegate
                                                     delegateQueue:nil];

    NSURLSessionDownloadTask *downloadTask = [session downloadTaskWithURL:downloadURL];
    [downloadTask resume];
}

#pragma mark - Cleanup

+ (BOOL)deleteModel:(NSString *)modelName {
    NSString *modelPath = [self localPathForModel:modelName];
    NSFileManager *fileManager = [NSFileManager defaultManager];

    if (![fileManager fileExistsAtPath:modelPath]) {
        return YES; // Already deleted
    }

    NSError *error = nil;
    BOOL success = [fileManager removeItemAtPath:modelPath error:&error];

    if (!success) {
        NSLog(@"VROModelDownloader: Failed to delete model %@: %@", modelName, error.localizedDescription);
    }

    return success;
}

+ (BOOL)deleteAllModels {
    NSString *modelsDir = [self modelsDirectory];
    NSFileManager *fileManager = [NSFileManager defaultManager];

    if (![fileManager fileExistsAtPath:modelsDir]) {
        return YES;
    }

    NSError *error = nil;
    NSArray *contents = [fileManager contentsOfDirectoryAtPath:modelsDir error:&error];

    if (error) {
        NSLog(@"VROModelDownloader: Failed to list models directory: %@", error.localizedDescription);
        return NO;
    }

    BOOL allSuccess = YES;
    for (NSString *item in contents) {
        NSString *itemPath = [modelsDir stringByAppendingPathComponent:item];
        NSError *deleteError = nil;

        if (![fileManager removeItemAtPath:itemPath error:&deleteError]) {
            NSLog(@"VROModelDownloader: Failed to delete %@: %@", item, deleteError.localizedDescription);
            allSuccess = NO;
        }
    }

    return allSuccess;
}

#pragma mark - Configuration

+ (NSString *)modelsDirectory {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *appSupportDir = paths.firstObject;
    NSString *modelsDir = [appSupportDir stringByAppendingPathComponent:@"ViroModels"];

    // Create directory if it doesn't exist
    NSFileManager *fileManager = [NSFileManager defaultManager];
    if (![fileManager fileExistsAtPath:modelsDir]) {
        NSError *error = nil;
        [fileManager createDirectoryAtPath:modelsDir
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&error];
        if (error) {
            NSLog(@"VROModelDownloader: Failed to create models directory: %@", error.localizedDescription);
        }
    }

    return modelsDir;
}

#pragma mark - Extraction

+ (BOOL)extractZipAtPath:(NSString *)zipPath
           toDestination:(NSString *)destPath
                   error:(NSError **)error {

    NSFileManager *fileManager = [NSFileManager defaultManager];

    // Create a temporary directory for extraction
    NSString *tempDir = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];

    NSError *dirError = nil;
    if (![fileManager createDirectoryAtPath:tempDir
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:&dirError]) {
        if (error) {
            *error = dirError;
        }
        return NO;
    }

    // iOS doesn't have NSTask, use manual extraction
    if (![self extractZipManually:zipPath toDirectory:tempDir error:error]) {
        [fileManager removeItemAtPath:tempDir error:nil];
        return NO;
    }

    // Find the extracted .mlmodelc directory
    NSArray *extractedContents = [fileManager contentsOfDirectoryAtPath:tempDir error:nil];
    NSString *modelcDir = nil;

    for (NSString *item in extractedContents) {
        if ([item hasSuffix:@".mlmodelc"]) {
            modelcDir = [tempDir stringByAppendingPathComponent:item];
            break;
        }
    }

    if (!modelcDir) {
        if (error) {
            *error = [NSError errorWithDomain:VROModelDownloaderErrorDomain
                                         code:VROModelDownloaderErrorExtractionFailed
                                     userInfo:@{NSLocalizedDescriptionKey: @"No .mlmodelc found in archive"}];
        }
        [fileManager removeItemAtPath:tempDir error:nil];
        return NO;
    }

    // Move to final destination
    NSError *moveError = nil;

    // Remove existing destination if present
    if ([fileManager fileExistsAtPath:destPath]) {
        [fileManager removeItemAtPath:destPath error:nil];
    }

    if (![fileManager moveItemAtPath:modelcDir toPath:destPath error:&moveError]) {
        if (error) {
            *error = moveError;
        }
        [fileManager removeItemAtPath:tempDir error:nil];
        return NO;
    }

    // Cleanup temp directory
    [fileManager removeItemAtPath:tempDir error:nil];

    return YES;
}

+ (BOOL)extractZipManually:(NSString *)zipPath
               toDirectory:(NSString *)destDir
                     error:(NSError **)error {
    // Simple zip extraction for iOS (uses compression framework)
    // This is a basic implementation - for production, consider using SSZipArchive or similar

    NSData *zipData = [NSData dataWithContentsOfFile:zipPath];
    if (!zipData) {
        if (error) {
            *error = [NSError errorWithDomain:VROModelDownloaderErrorDomain
                                         code:VROModelDownloaderErrorExtractionFailed
                                     userInfo:@{NSLocalizedDescriptionKey: @"Could not read zip file"}];
        }
        return NO;
    }

    // For iOS, we'll use a simpler approach - expect the server to provide
    // an uncompressed directory structure or use a third-party library

    // Alternative: Use Apple's compression framework for individual files
    // For now, return error and suggest using SSZipArchive pod

    if (error) {
        *error = [NSError errorWithDomain:VROModelDownloaderErrorDomain
                                     code:VROModelDownloaderErrorExtractionFailed
                                 userInfo:@{NSLocalizedDescriptionKey:
                                     @"Zip extraction requires SSZipArchive. Add to Podfile: pod 'SSZipArchive'"}];
    }

    return NO;
}

@end

#pragma mark - Download Delegate Implementation

@implementation VROModelDownloadDelegate

- (instancetype)initWithModelName:(NSString *)modelName
                         progress:(VROModelDownloadProgress)progressBlock
                       completion:(VROModelDownloadCompletion)completionBlock {
    self = [super init];
    if (self) {
        _modelName = [modelName copy];
        _progressBlock = [progressBlock copy];
        _completionBlock = [completionBlock copy];
    }
    return self;
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
didFinishDownloadingToURL:(NSURL *)location {

    NSString *destPath = [VROModelDownloader localPathForModel:self.modelName];
    NSString *tempZipPath = [NSTemporaryDirectory() stringByAppendingPathComponent:
                             [NSString stringWithFormat:@"%@.zip", [[NSUUID UUID] UUIDString]]];

    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSError *error = nil;

    // Move downloaded file to temp location
    if (![fileManager moveItemAtURL:location toURL:[NSURL fileURLWithPath:tempZipPath] error:&error]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.completionBlock(nil, error);
        });
        return;
    }

    // Extract the zip file
    if (![VROModelDownloader extractZipAtPath:tempZipPath toDestination:destPath error:&error]) {
        [fileManager removeItemAtPath:tempZipPath error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.completionBlock(nil, error);
        });
        return;
    }

    // Cleanup zip file
    [fileManager removeItemAtPath:tempZipPath error:nil];

    // Success
    dispatch_async(dispatch_get_main_queue(), ^{
        self.completionBlock(destPath, nil);
    });
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
      didWriteData:(int64_t)bytesWritten
 totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {

    if (self.progressBlock && totalBytesExpectedToWrite > 0) {
        float progress = (float)totalBytesWritten / (float)totalBytesExpectedToWrite;
        dispatch_async(dispatch_get_main_queue(), ^{
            self.progressBlock(progress);
        });
    }
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error {

    if (error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.completionBlock(nil, error);
        });
    }
}

@end
