# 隐藏真实的源文件名
-renamesourcefileattribute SourceFile

# 移除行号信息
-keepattributes Exceptions,InnerClasses,Signature,Deprecated

# 保留 Kotlin 的元数据，确保 Kotlin 协程、高阶函数、默认参数在外部能正常调用
-keep class kotlin.Metadata { *; }

-dontwarn java.lang.invoke.StringConcatFactory
