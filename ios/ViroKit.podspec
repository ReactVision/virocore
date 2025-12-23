Pod::Spec.new do |s|
  s.name                = 'ViroKit'
  s.version             = '1.0'
  s.summary             = 'Framework containing the ViroRenderer'
  s.description         = <<-DESC
    ViroKit is the core rendering framework for ViroReact.

    ARCore SDK Integration (Optional):
    ViroKit is built with weak linking for ARCore frameworks, making them optional.
    To enable Cloud Anchors, Geospatial, or Scene Semantics features, add the
    appropriate ARCore pods to your Podfile:

      pod 'ARCore/CloudAnchors', '~> 1.51.0'  # For Cloud Anchors
      pod 'ARCore/Geospatial', '~> 1.51.0'    # For Geospatial API
      pod 'ARCore/Semantics', '~> 1.51.0'     # For Scene Semantics

    Without these pods, ViroKit works normally but ARCore features return
    isAvailable = false at runtime.
  DESC
  s.source              = { :path => '.' } # source is required, but path will be defined in user's Podfile (this value will be ignored).
  s.vendored_frameworks = 'ViroKit.framework'
  s.homepage            = 'https://reactvision.xyz'
  s.license             = {:type => 'Copyright', :text => "Copyright 2025 ReactVision" }
  s.author              = 'ReactVision'
  s.requires_arc        = true
  s.platform            = :ios, '13.0'
  s.dependency 'React'

  # ARCore frameworks and Google dependencies are weak-linked, making them optional at runtime
  # Apps can choose to include ARCore pods for Cloud Anchors, Geospatial, and Semantics features
  # Google dependencies (FBLPromises, etc.) are also weak-linked for true optional ARCore support
  # Note: ARCore is distributed as separate sub-frameworks, not a single ARCore.framework
  s.weak_frameworks     = ['ARCoreBase', 'ARCoreGARSession', 'ARCoreCloudAnchors',
                           'ARCoreGeospatial', 'ARCoreSemantics', 'ARCoreTFShared',
                           'FBLPromises', 'GoogleDataTransport', 'GoogleUtilities']
end
